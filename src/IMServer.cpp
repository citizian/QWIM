#include "IMServer.h"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

void IMServer::setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    std::cerr << "fcntl F_GETFL failed\n";
    return;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    std::cerr << "fcntl F_SETFL failed\n";
  }
}

IMServer::IMServer(const std::string &config_file) : m_running(false) {
  loadConfig(config_file);

  std::string logfile = "logs/server.log";
  if (m_config.count("logfile")) {
    logfile = m_config["logfile"];
  }
  m_log_file.open(logfile, std::ios::app);

  m_port = 8080;
  if (m_config.count("port")) {
    m_port = std::stoi(m_config["port"]);
  }

  m_loop = std::make_unique<EventLoop>();

  // 1. socket
  m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_server_fd == -1) {
    log("ERROR", "Failed to create socket");
    exit(1);
  }

  // 允许端口复用
  int opt = 1;
  setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  setNonBlocking(m_server_fd);

  // 设置服务器地址和端口
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(m_port);

  // 2. bind
  if (bind(m_server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
    log("ERROR", "Failed to bind socket");
    close(m_server_fd);
    exit(1);
  }

  // 3. listen
  if (listen(m_server_fd, SOMAXCONN) == -1) {
    std::cerr << "Failed to listen\n";
    close(m_server_fd);
    exit(1);
  }

  m_server_channel = std::make_unique<Channel>(m_loop.get(), m_server_fd);
  m_server_channel->setReadCallback(
      std::bind(&IMServer::handleNewConnection, this));
}

IMServer::~IMServer() {
  if (m_log_file.is_open()) {
    m_log_file.close();
  }
  m_running = false;
  close(m_server_fd);
}

void IMServer::removeClient(int cfd) {
  if (m_connections.count(cfd)) {
    auto &conn = m_connections[cfd];
    if (!conn->username.empty()) {
      std::string user = conn->username;
      m_online_users.erase(user);

      nlohmann::json sys_j;
      sys_j["type"] = "system";
      sys_j["msg"] = user + " left";
      std::string sys_str = sys_j.dump();
      broadcastMessage(cfd, sys_str.c_str(), sys_str.length());
    }

    m_loop->removeChannel(conn->channel.get());
    m_connections.erase(cfd);
  }
}

void IMServer::handleNewConnection() {
  while (true) {
    sockaddr_in client_address{};
    socklen_t client_len = sizeof(client_address);
    int client_fd =
        accept(m_server_fd, (struct sockaddr *)&client_address, &client_len);

    if (client_fd == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      log("ERROR", "Failed to accept connection");
      break;
    }

    setNonBlocking(client_fd);

    m_connections[client_fd] =
        std::make_unique<Connection>(m_loop.get(), client_fd);
    m_connections[client_fd]->setReadCallback(
        std::bind(&IMServer::onConnectionMessage, this, std::placeholders::_1));
    m_connections[client_fd]->setCloseCallback(
        std::bind(&IMServer::onConnectionClose, this, std::placeholders::_1));

    log("INFO", "Client " + std::to_string(client_fd) + " connected!");
  }
}

void IMServer::onConnectionClose(Connection *conn) {
  log("INFO", "Client " + std::to_string(conn->fd) + " disconnected");
  removeClient(conn->fd);
}

void IMServer::onConnectionMessage(Connection *conn) {
  while (conn->input_buffer.readableBytes() >= 4) {
    uint32_t net_len;
    memcpy(&net_len, conn->input_buffer.peek(), 4);
    uint32_t len = ntohl(net_len);

    if (conn->input_buffer.readableBytes() >= 4 + len) {
      conn->input_buffer.retrieve(4); // Consume header length
      std::string message =
          conn->input_buffer.readAsString(len); // Consume payload

      try {
        nlohmann::json j = nlohmann::json::parse(message);
        conn->last_active = time(nullptr);
        std::string type = j.value("type", "");

        if (type == "login") {
          std::string user = j.value("user", "Unknown");
          conn->username = user;
          m_online_users[user] = conn->fd;
          log("INFO", "User '" + user + "' logged in on fd " +
                          std::to_string(conn->fd));

          nlohmann::json sys_j;
          sys_j["type"] = "system";
          sys_j["msg"] = user + " joined";
          std::string sys_str = sys_j.dump();
          broadcastMessage(conn->fd, sys_str.c_str(), sys_str.length());

          for (const std::string &hist_msg : m_message_history) {
            nlohmann::json hist_j;
            hist_j["type"] = "history";
            hist_j["msg"] = hist_msg;
            std::string hist_str = hist_j.dump();

            uint32_t hist_len = hist_str.length();
            uint32_t nl = htonl(hist_len);
            std::vector<char> packet(4 + hist_len);
            memcpy(packet.data(), &nl, 4);
            memcpy(packet.data() + 4, hist_str.c_str(), hist_len);
            conn->write_data(packet.data(), packet.size());
          }
        } else if (type == "chat") {
          std::string msg = j.value("msg", "");
          std::string sender =
              conn->username.empty() ? "Unknown" : conn->username;

          log("CHAT", "[" + sender + " to all]: " + msg);

          nlohmann::json chat_j;
          chat_j["type"] = "chat";
          chat_j["user"] = sender;
          chat_j["msg"] = msg;
          std::string chat_str = chat_j.dump();

          std::string history_entry = sender + ": " + msg;
          m_message_history.push_back(history_entry);

          size_t history_size = 50;
          if (m_config.count("history_size")) {
            history_size = std::stoull(m_config["history_size"]);
          }

          while (m_message_history.size() > history_size) {
            m_message_history.pop_front();
          }

          broadcastMessage(conn->fd, chat_str.c_str(), chat_str.length());
        } else if (type == "private") {
          std::string to_user = j.value("to", "");
          std::string msg = j.value("msg", "");
          std::string sender =
              conn->username.empty() ? "Unknown" : conn->username;

          log("CHAT", "[" + sender + " -> " + to_user + "]: " + msg);

          int target_fd = -1;
          if (m_online_users.count(to_user)) {
            target_fd = m_online_users[to_user];
          }

          if (target_fd != -1 && m_connections.count(target_fd)) {
            nlohmann::json private_j;
            private_j["type"] = "private";
            private_j["user"] = sender;
            private_j["msg"] = msg;
            std::string private_str = private_j.dump();

            uint32_t n_len = htonl(private_str.length());
            std::vector<char> packet(4 + private_str.length());
            memcpy(packet.data(), &n_len, 4);
            memcpy(packet.data() + 4, private_str.c_str(),
                   private_str.length());

            m_connections[target_fd]->write_data(packet.data(), packet.size());
          } else {
            nlohmann::json err_j;
            err_j["type"] = "system";
            err_j["msg"] = "user not online";
            std::string err_str = err_j.dump();

            uint32_t n_len = htonl(err_str.length());
            std::vector<char> packet(4 + err_str.length());
            memcpy(packet.data(), &n_len, 4);
            memcpy(packet.data() + 4, err_str.c_str(), err_str.length());

            conn->write_data(packet.data(), packet.size());
            log("INFO",
                "User '" + to_user + "' not found. Error sent to sender.");
          }
        } else if (type == "list") {
          nlohmann::json list_j;
          list_j["type"] = "list";
          list_j["users"] = nlohmann::json::array();

          for (const auto &pair : m_online_users) {
            list_j["users"].push_back(pair.first);
          }

          std::string list_str = list_j.dump();
          uint32_t n_len = htonl(list_str.length());
          std::vector<char> packet(4 + list_str.length());
          memcpy(packet.data(), &n_len, 4);
          memcpy(packet.data() + 4, list_str.c_str(), list_str.length());

          conn->write_data(packet.data(), packet.size());
          log("INFO",
              "Sent active users list to fd " + std::to_string(conn->fd));
        } else if (type == "heartbeat") {
          conn->last_active = time(nullptr);
        }
      } catch (const nlohmann::json::parse_error &e) {
        log("ERROR", "JSON parse error from fd " + std::to_string(conn->fd) +
                         ": " + e.what());
      }
    } else {
      break;
    }
  }
}

void IMServer::start() {
  log("INFO", "Server is listening on port " + std::to_string(m_port) + "...");
  m_running = true;

  m_server_channel->enableReading();

  m_loop->setTickCallback([this]() {
    time_t now = time(nullptr);
    std::vector<int> to_remove;
    int heartbeat_timeout = 30;
    if (m_config.count("heartbeat_timeout")) {
      heartbeat_timeout = std::stoi(m_config["heartbeat_timeout"]);
    }

    for (const auto &pair : m_connections) {
      if (now - pair.second->last_active > heartbeat_timeout) {
        to_remove.push_back(pair.first);
      }
    }

    for (int fd : to_remove) {
      log("INFO", "Client " + std::to_string(fd) +
                      " heartbeat timeout. Disconnecting.");
      removeClient(fd);
    }
  });

  m_loop->loop();
}

void IMServer::broadcastMessage(int sender_fd, const char *message,
                                uint32_t len) {
  uint32_t net_len = htonl(len);
  std::vector<char> packet(4 + len);
  memcpy(packet.data(), &net_len, 4);
  memcpy(packet.data() + 4, message, len);

  for (const auto &pair : m_online_users) {
    int fd = pair.second;
    if (fd != sender_fd && m_connections.count(fd)) {
      m_connections[fd]->write_data(packet.data(), packet.size());
    }
  }
}

void IMServer::log(const std::string &level, const std::string &msg) {
  time_t now = time(nullptr);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", localtime(&now));
  std::string log_line = "[" + std::string(buf) + "] [" + level + "] " + msg;
  std::cout << log_line << std::endl;
  if (m_log_file.is_open()) {
    m_log_file << log_line << std::endl;
    m_log_file.flush();
  }
}

void IMServer::loadConfig(const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cerr << "Warning: Could not open config file " << filename
              << ", using defaults.\n";
    return;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '#')
      continue;

    size_t delimiterPos = line.find('=');
    if (delimiterPos != std::string::npos) {
      std::string key = line.substr(0, delimiterPos);
      std::string value = line.substr(delimiterPos + 1);

      key.erase(0, key.find_first_not_of(" \t"));
      key.erase(key.find_last_not_of(" \t") + 1);
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t") + 1);

      m_config[key] = value;
    }
  }
  file.close();
}

// Removed main function
