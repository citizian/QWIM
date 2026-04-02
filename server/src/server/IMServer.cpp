#include "IMServer.h"
#include "AsyncLogger.h"
#include "ChatService.h"
#include "Config.h"
#include "Logger.h"
#include "Router.h"
#include <cstring>

std::unique_ptr<AsyncLogger> g_asyncLogger;

void asyncOutput(const char *msg, int len) {
  if (g_asyncLogger) {
    g_asyncLogger->append(msg, len);
  }
}

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
  Config::instance().load(config_file);

  std::string logfile =
      Config::instance().getString("logfile", "logs/server.log");

  std::string log_level = Config::instance().getString("log_level", "INFO");
  if (log_level == "TRACE")
    Logger::setLogLevel(TRACE);
  else if (log_level == "DEBUG")
    Logger::setLogLevel(DEBUG);
  else if (log_level == "INFO")
    Logger::setLogLevel(INFO);
  else if (log_level == "WARN")
    Logger::setLogLevel(WARN);
  else if (log_level == "ERROR")
    Logger::setLogLevel(ERROR);
  else if (log_level == "FATAL")
    Logger::setLogLevel(FATAL);

  g_asyncLogger.reset(new AsyncLogger(logfile, 100 * 1024 * 1024));
  Logger::setOutput(asyncOutput);
  g_asyncLogger->start();

  m_port = Config::instance().getInt("port", 8081);

  m_loop = std::make_unique<EventLoop>();
  m_thread_pool = std::make_unique<EventLoopThreadPool>(m_loop.get());
  m_timer_manager = std::make_unique<TimerManager>();

  ChatService::instance().init();

  // 1. socket
  m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_server_fd == -1) {
    LOG_ERROR << "Failed to create socket";
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
    LOG_ERROR << "Failed to bind socket";
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
  if (g_asyncLogger) {
    g_asyncLogger->stop();
    g_asyncLogger.reset();
  }
  m_running = false;
  close(m_server_fd);
}

void IMServer::removeClient(int cfd) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_connections.count(cfd)) {
    ChatService::instance().onDisconnect(m_connections[cfd]);

    m_connections[cfd]->channel->disableWriting(); // force clean logic
    m_connections[cfd]->channel->getLoop()->removeChannel(
        m_connections[cfd]->channel.get()); // The subloop cleans his epoll
    m_timer_manager->removeTimer(cfd);
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
      LOG_ERROR << "Failed to accept connection";
      break;
    }

    setNonBlocking(client_fd);

    EventLoop *io_loop = m_thread_pool->getNextLoop();

    {
      std::lock_guard<std::mutex> lock(m_mutex);
      auto conn = std::make_shared<Connection>(io_loop, client_fd);
      m_connections[client_fd] = conn;
      conn->channel->tie(conn);

      m_connections[client_fd]->setReadCallback(std::bind(
          &IMServer::onConnectionMessage, this, std::placeholders::_1));
      m_connections[client_fd]->setCloseCallback(
          std::bind(&IMServer::onConnectionClose, this, std::placeholders::_1));

      int timeout = Config::instance().getInt("heartbeat_timeout", 30);
      m_timer_manager->addTimer(conn, time(nullptr) + timeout);
    }

    LOG_INFO << "Client " + std::to_string(client_fd) + " connected!";
  }
}

void IMServer::onConnectionClose(std::shared_ptr<Connection> conn) {
  LOG_INFO << "Client " + std::to_string(conn->fd) + " disconnected";
  removeClient(conn->fd);
}

void IMServer::onConnectionMessage(std::shared_ptr<Connection> conn) {
  while (true) {
    if (conn->input_buffer.readableBytes() < 4) {
      break;
    }

    uint32_t net_len;
    memcpy(&net_len, conn->input_buffer.peek(), 4);
    uint32_t len = ntohl(net_len);

    if (conn->input_buffer.readableBytes() >= 4 + len) {
      conn->input_buffer.retrieve(4); // Consume header length
      std::string message =
          conn->input_buffer.readAsString(len); // Consume payload

      try {
        nlohmann::json j = nlohmann::json::parse(message);

        {
          std::lock_guard<std::mutex> lock(m_mutex);
          conn->last_active = time(nullptr);
          int timeout = Config::instance().getInt("heartbeat_timeout", 30);
          m_timer_manager->addTimer(conn, time(nullptr) + timeout);
        }

        std::string type = j.value("type", "");
        Router::instance().route(type, conn, j, this);

      } catch (const nlohmann::json::parse_error &e) {
        LOG_ERROR << "JSON parse error from fd " + std::to_string(conn->fd) +
                         ": " + e.what();
      }
    } else {
      break;
    }
  }
}

void IMServer::start() {
  LOG_INFO << "Server is listening on port " + std::to_string(m_port) + "...";
  m_running = true;

  int thread_num = Config::instance().getInt("num_threads", 0);
  m_thread_pool->setThreadNum(thread_num);
  m_thread_pool->start();

  m_server_channel->enableReading();

  m_loop->setTickCallback([this]() {
    time_t now = time(nullptr);
    std::vector<std::shared_ptr<Connection>> to_remove;
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      to_remove = m_timer_manager->checkTimeout(now);
    }

    for (auto &conn : to_remove) {
      LOG_INFO << "Client " + std::to_string(conn->fd) +
                      " heartbeat timeout. Disconnecting.";
      removeClient(conn->fd);
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

  std::vector<int> targets;
  {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &pair : m_connections) {
      if (pair.first != sender_fd) {
        targets.push_back(pair.first);
      }
    }
  }

  for (int fd : targets) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_connections.count(fd)) {
      m_connections[fd]->write_data(packet.data(), packet.size());
    }
  }
}

bool IMServer::isConnectionActive(int fd) {
  std::lock_guard<std::mutex> lock(m_mutex);
  return m_connections.count(fd) > 0;
}

void IMServer::sendToUser(int target_fd, const char *data, size_t len) {
  std::lock_guard<std::mutex> lock(m_mutex);
  if (m_connections.count(target_fd)) {
    m_connections[target_fd]->write_data(data, len);
  }
}

// Removed main function
