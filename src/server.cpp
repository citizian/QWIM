#ifndef SERVER_H
#define SERVER_H

#include "json.hpp"
#include <ctime>
#include <deque>
#include <fstream>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

class Server {
public:
  Server(const std::string &config_file);
  ~Server();

  void start();

private:
  void broadcastMessage(int sender_fd, const char *message, uint32_t len);
  void setNonBlocking(int fd);

  int m_server_fd;
  int m_port;
  bool m_running;

  int m_epoll_fd;
  std::vector<int> m_clients;
  std::unordered_map<int, std::string> m_client_buffers;
  std::unordered_map<int, std::string> m_client_names;
  std::unordered_map<std::string, int> m_online_users;
  std::unordered_map<int, time_t> m_client_last_active;
  std::deque<std::string> m_message_history;
  std::unordered_map<std::string, std::string> m_config;

  std::ofstream m_log_file;
  void log(const std::string &level, const std::string &msg);
  void loadConfig(const std::string &filename);
};

#endif // SERVER_H
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

void Server::setNonBlocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    std::cerr << "fcntl F_GETFL failed\n";
    return;
  }
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    std::cerr << "fcntl F_SETFL failed\n";
  }
}

Server::Server(const std::string &config_file) : m_running(false) {
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

  // 初始化 epoll
  m_epoll_fd = epoll_create1(0);
  if (m_epoll_fd == -1) {
    log("ERROR", "Failed to create epoll");
    close(m_server_fd);
    exit(1);
  }

  // 设置服务器地址和端口
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(m_port);

  // 2. bind
  if (bind(m_server_fd, (struct sockaddr *)&address, sizeof(address)) == -1) {
    log("ERROR", "Failed to bind socket");
    close(m_epoll_fd);
    close(m_server_fd);
    exit(1);
  }

  // 3. listen
  if (listen(m_server_fd, SOMAXCONN) == -1) {
    std::cerr << "Failed to listen\n";
    close(m_epoll_fd);
    close(m_server_fd);
    exit(1);
  }
}

Server::~Server() {
  if (m_log_file.is_open()) {
    m_log_file.close();
  }
  m_running = false;
  close(m_epoll_fd);
  close(m_server_fd);
}

void Server::start() {
  log("INFO", "Server is listening on port " + std::to_string(m_port) + "...");
  m_running = true;

  // 将监听套接字加入 epoll
  epoll_event event;
  event.events = EPOLLIN | EPOLLET; // 加 ET 模式
  event.data.fd = m_server_fd;
  if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_server_fd, &event) == -1) {
    log("ERROR", "Failed to add listen socket to epoll");
    return;
  }

  const int MAX_EVENTS = 10;
  epoll_event events[MAX_EVENTS];

  while (m_running) {
    int nfds = epoll_wait(m_epoll_fd, events, MAX_EVENTS,
                          1000); // 1000ms timeout default

    if (nfds == -1) {
      if (errno == EINTR)
        continue; // 可能是被信号中断
      log("ERROR", "epoll_wait error");
      break;
    }

    time_t now = time(nullptr);
    std::vector<int> to_remove;

    int heartbeat_timeout = 30;
    if (m_config.count("heartbeat_timeout")) {
      heartbeat_timeout = std::stoi(m_config["heartbeat_timeout"]);
    }

    for (const auto &pair : m_client_last_active) {
      if (now - pair.second > heartbeat_timeout) {
        to_remove.push_back(pair.first);
      }
    }

    for (int fd : to_remove) {
      log("INFO", "Client " + std::to_string(fd) +
                      " heartbeat timeout. Disconnecting.");
      epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
      m_clients.erase(std::remove(m_clients.begin(), m_clients.end(), fd),
                      m_clients.end());
      m_client_buffers.erase(fd);
      if (m_client_names.count(fd)) {
        std::string user = m_client_names[fd];
        m_online_users.erase(user);

        nlohmann::json sys_j;
        sys_j["type"] = "system";
        sys_j["msg"] = user + " left";
        std::string sys_str = sys_j.dump();
        broadcastMessage(fd, sys_str.c_str(), sys_str.length());
      }
      m_client_names.erase(fd);
      m_client_last_active.erase(fd);
      close(fd);
    }

    for (int i = 0; i < nfds; ++i) {
      int fd = events[i].data.fd;

      if (fd == m_server_fd) {
        // 新客户端连接
        while (true) {
          sockaddr_in client_address{};
          socklen_t client_len = sizeof(client_address);
          int client_fd = accept(
              m_server_fd, (struct sockaddr *)&client_address, &client_len);

          if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
              // 处理完所有连接
              break;
            }
            log("ERROR", "Failed to accept connection");
            break;
          }

          setNonBlocking(client_fd);

          epoll_event client_event;
          // 监听可读，挂起以及设为 ET 模式
          client_event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
          client_event.data.fd = client_fd;

          if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) ==
              -1) {
            log("ERROR", "Failed to add client fd to epoll");
            close(client_fd);
            continue; // 注意: 继续 accept，而不是退出
          }

          m_clients.push_back(client_fd);
          m_client_last_active[client_fd] = time(nullptr);
          log("INFO", "Client " + std::to_string(client_fd) + " connected!");
        }
      } else {
        // 客户端事件
        if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
          // 客户端断开连接或发生错误
          log("INFO", "Client " + std::to_string(fd) + " disconnected");
          epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd,
                    nullptr); // 可选，close 自动从 epoll 中移除
          m_clients.erase(std::remove(m_clients.begin(), m_clients.end(), fd),
                          m_clients.end());
          m_client_buffers.erase(fd);
          if (m_client_names.count(fd)) {
            std::string user = m_client_names[fd];
            m_online_users.erase(user);

            nlohmann::json sys_j;
            sys_j["type"] = "system";
            sys_j["msg"] = user + " left";
            std::string sys_str = sys_j.dump();
            broadcastMessage(fd, sys_str.c_str(), sys_str.length());
          }
          m_client_names.erase(fd);
          m_client_last_active.erase(fd);
          close(fd);
        } else if (events[i].events & EPOLLIN) {
          // 客户端发送消息
          char buffer[1024] = {0};
          while (true) {
            ssize_t bytes_read = read(fd, buffer, sizeof(buffer));

            if (bytes_read > 0) {
              m_client_buffers[fd].append(buffer, bytes_read);

              // 尝试解析完整的消息
              while (m_client_buffers[fd].size() >= 4) {
                uint32_t net_len;
                memcpy(&net_len, m_client_buffers[fd].data(), 4);
                uint32_t len = ntohl(net_len);

                if (m_client_buffers[fd].size() >= 4 + len) {
                  std::string message = m_client_buffers[fd].substr(4, len);
                  m_client_buffers[fd].erase(0, 4 + len);

                  try {
                    nlohmann::json j = nlohmann::json::parse(message);

                    // 只要收到有效的 JSON 消息，就更新活跃时间
                    m_client_last_active[fd] = time(nullptr);

                    std::string type = j.value("type", "");

                    if (type == "login") {
                      std::string user = j.value("user", "Unknown");
                      m_client_names[fd] = user;
                      m_online_users[user] = fd;
                      log("INFO", "User '" + user + "' logged in on fd " +
                                      std::to_string(fd));

                      nlohmann::json sys_j;
                      sys_j["type"] = "system";
                      sys_j["msg"] = user + " joined";
                      std::string sys_str = sys_j.dump();
                      broadcastMessage(fd, sys_str.c_str(), sys_str.length());

                      for (const std::string &hist_msg : m_message_history) {
                        nlohmann::json hist_j;
                        hist_j["type"] = "history";
                        hist_j["msg"] = hist_msg;
                        std::string hist_str = hist_j.dump();

                        uint32_t hist_len = hist_str.length();
                        uint32_t net_len = htonl(hist_len);
                        std::vector<char> packet(4 + hist_len);
                        memcpy(packet.data(), &net_len, 4);
                        memcpy(packet.data() + 4, hist_str.c_str(), hist_len);
                        send(fd, packet.data(), packet.size(), 0);
                      }
                    } else if (type == "chat") {
                      std::string msg = j.value("msg", "");
                      std::string sender = m_client_names.count(fd)
                                               ? m_client_names[fd]
                                               : "Unknown";

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

                      broadcastMessage(fd, chat_str.c_str(), chat_str.length());
                    } else if (type == "private") {
                      std::string to_user = j.value("to", "");
                      std::string msg = j.value("msg", "");
                      std::string sender = m_client_names.count(fd)
                                               ? m_client_names[fd]
                                               : "Unknown";

                      log("CHAT",
                          "[" + sender + " -> " + to_user + "]: " + msg);

                      // Find the target fd by username O(1) leveraging
                      // online_users_
                      int target_fd = -1;
                      if (m_online_users.count(to_user)) {
                        target_fd = m_online_users[to_user];
                      }

                      if (target_fd != -1) {
                        nlohmann::json private_j;
                        private_j["type"] = "private";
                        private_j["user"] = sender;
                        private_j["msg"] = msg;
                        std::string private_str = private_j.dump();

                        uint32_t net_len = htonl(private_str.length());
                        std::vector<char> packet(4 + private_str.length());
                        memcpy(packet.data(), &net_len, 4);
                        memcpy(packet.data() + 4, private_str.c_str(),
                               private_str.length());

                        send(target_fd, packet.data(), packet.size(), 0);
                      } else {
                        // Target user is offline, send error to sender
                        nlohmann::json err_j;
                        err_j["type"] = "system";
                        err_j["msg"] = "user not online";
                        std::string err_str = err_j.dump();

                        uint32_t net_len = htonl(err_str.length());
                        std::vector<char> packet(4 + err_str.length());
                        memcpy(packet.data(), &net_len, 4);
                        memcpy(packet.data() + 4, err_str.c_str(),
                               err_str.length());

                        send(fd, packet.data(), packet.size(), 0);
                        log("INFO", "User '" + to_user +
                                        "' not found. Error sent to sender.");
                      }
                    } else if (type == "list") {
                      nlohmann::json list_j;
                      list_j["type"] = "list";
                      list_j["users"] = nlohmann::json::array();

                      for (const auto &pair : m_online_users) {
                        list_j["users"].push_back(pair.first);
                      }

                      std::string list_str = list_j.dump();
                      uint32_t net_len = htonl(list_str.length());
                      std::vector<char> packet(4 + list_str.length());
                      memcpy(packet.data(), &net_len, 4);
                      memcpy(packet.data() + 4, list_str.c_str(),
                             list_str.length());

                      send(fd, packet.data(), packet.size(), 0);
                      log("INFO",
                          "Sent active users list to fd " + std::to_string(fd));
                    } else if (type == "heartbeat") {
                      m_client_last_active[fd] = time(nullptr);
                    }
                  } catch (const nlohmann::json::parse_error &e) {
                    log("ERROR", "JSON parse error from fd " +
                                     std::to_string(fd) + ": " + e.what());
                  }
                } else {
                  break; // 消息不完整，等待更多数据
                }
              }
            } else if (bytes_read == -1) {
              if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // ET 模式下数据读完必须收到这个错误
              }
              log("ERROR", "Read error on client " + std::to_string(fd));

              // 错误处理，关闭客户端
              epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
              m_clients.erase(
                  std::remove(m_clients.begin(), m_clients.end(), fd),
                  m_clients.end());
              m_client_buffers.erase(fd);
              if (m_client_names.count(fd)) {
                std::string user = m_client_names[fd];
                m_online_users.erase(user);

                nlohmann::json sys_j;
                sys_j["type"] = "system";
                sys_j["msg"] = user + " left";
                std::string sys_str = sys_j.dump();
                broadcastMessage(fd, sys_str.c_str(), sys_str.length());
              }
              m_client_names.erase(fd);
              m_client_last_active.erase(fd);
              close(fd);
              break;
            } else if (bytes_read == 0) {
              // 客户端关闭，通常也可以通过 EPOLLRDHUP 处理
              log("INFO",
                  "Client " + std::to_string(fd) + " disconnected (EOF)");
              epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
              m_clients.erase(
                  std::remove(m_clients.begin(), m_clients.end(), fd),
                  m_clients.end());
              m_client_buffers.erase(fd);
              if (m_client_names.count(fd)) {
                std::string user = m_client_names[fd];
                m_online_users.erase(user);

                nlohmann::json sys_j;
                sys_j["type"] = "system";
                sys_j["msg"] = user + " left";
                std::string sys_str = sys_j.dump();
                broadcastMessage(fd, sys_str.c_str(), sys_str.length());
              }
              m_client_names.erase(fd);
              m_client_last_active.erase(fd);
              close(fd);
              break;
            }
          }
        }
      }
    }
  }
}

void Server::broadcastMessage(int sender_fd, const char *message,
                              uint32_t len) {
  uint32_t net_len = htonl(len);
  std::vector<char> packet(4 + len);
  memcpy(packet.data(), &net_len, 4);
  memcpy(packet.data() + 4, message, len);

  for (const auto &pair : m_online_users) {
    int fd = pair.second;
    if (fd != sender_fd) {
      send(fd, packet.data(), packet.size(), 0);
    }
  }
}

void Server::log(const std::string &level, const std::string &msg) {
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

void Server::loadConfig(const std::string &filename) {
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

      // Trim whitespace (optional but good context)
      key.erase(0, key.find_first_not_of(" \t"));
      key.erase(key.find_last_not_of(" \t") + 1);
      value.erase(0, value.find_first_not_of(" \t"));
      value.erase(value.find_last_not_of(" \t") + 1);

      m_config[key] = value;
    }
  }
  file.close();
}

int main() {
  Server server("config/server.conf");
  server.start();

  return 0;
}
