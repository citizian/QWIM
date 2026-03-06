#include "../include/Server.h"
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

Server::Server(int port) : m_port(port), m_running(false) {
  // 1. socket
  m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (m_server_fd == -1) {
    std::cerr << "Failed to create socket\n";
    exit(1);
  }

  // 允许端口复用
  int opt = 1;
  setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  setNonBlocking(m_server_fd);

  // 初始化 epoll
  m_epoll_fd = epoll_create1(0);
  if (m_epoll_fd == -1) {
    std::cerr << "Failed to create epoll\n";
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
    std::cerr << "Failed to bind socket\n";
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
  m_running = false;
  close(m_epoll_fd);
  close(m_server_fd);
}

void Server::start() {
  std::cout << "Server is listening on port " << m_port << "...\n";
  m_running = true;

  // 将监听套接字加入 epoll
  epoll_event event;
  event.events = EPOLLIN | EPOLLET; // 加 ET 模式
  event.data.fd = m_server_fd;
  if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, m_server_fd, &event) == -1) {
    std::cerr << "Failed to add listen socket to epoll\n";
    return;
  }

  const int MAX_EVENTS = 10;
  epoll_event events[MAX_EVENTS];

  while (m_running) {
    int nfds = epoll_wait(m_epoll_fd, events, MAX_EVENTS, -1);

    if (nfds == -1) {
      if (errno == EINTR)
        continue; // 可能是被信号中断
      std::cerr << "epoll_wait error\n";
      break;
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
            std::cerr << "Failed to accept connection\n";
            break;
          }

          setNonBlocking(client_fd);

          epoll_event client_event;
          // 监听可读，挂起以及设为 ET 模式
          client_event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
          client_event.data.fd = client_fd;

          if (epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, client_fd, &client_event) ==
              -1) {
            std::cerr << "Failed to add client fd to epoll\n";
            close(client_fd);
            continue; // 注意: 继续 accept，而不是退出
          }

          m_clients.push_back(client_fd);
          std::cout << "Client " << client_fd << " connected!\n";
        }
      } else {
        // 客户端事件
        if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
          // 客户端断开连接或发生错误
          std::cout << "Client " << fd << " disconnected\n";
          epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd,
                    nullptr); // 可选，close 自动从 epoll 中移除
          m_clients.erase(std::remove(m_clients.begin(), m_clients.end(), fd),
                          m_clients.end());
          m_client_buffers.erase(fd);
          m_client_names.erase(fd);
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
                    std::string type = j.value("type", "");

                    if (type == "login") {
                      std::string user = j.value("user", "Unknown");
                      m_client_names[fd] = user;
                      std::cout << "User '" << user << "' logged in on fd "
                                << fd << std::endl;
                    } else if (type == "chat") {
                      std::string msg = j.value("msg", "");
                      std::string sender = m_client_names.count(fd)
                                               ? m_client_names[fd]
                                               : "Unknown";
                      std::cout << "[" << sender << "]: " << msg << std::endl;

                      nlohmann::json broadcast_j;
                      broadcast_j["type"] = "chat";
                      broadcast_j["user"] = sender;
                      broadcast_j["msg"] = msg;
                      std::string broadcast_str = broadcast_j.dump();

                      broadcastMessage(fd, broadcast_str.c_str(),
                                       broadcast_str.length());
                    }
                  } catch (const nlohmann::json::parse_error &e) {
                    std::cerr << "JSON parse error from fd " << fd << ": "
                              << e.what() << "\n";
                  }
                } else {
                  break; // 消息不完整，等待更多数据
                }
              }
            } else if (bytes_read == -1) {
              if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // ET 模式下数据读完必须收到这个错误
              }
              std::cerr << "Read error on client " << fd << "\n";

              // 错误处理，关闭客户端
              epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
              m_clients.erase(
                  std::remove(m_clients.begin(), m_clients.end(), fd),
                  m_clients.end());
              m_client_buffers.erase(fd);
              m_client_names.erase(fd);
              close(fd);
              break;
            } else if (bytes_read == 0) {
              // 客户端关闭，通常也可以通过 EPOLLRDHUP 处理
              std::cout << "Client " << fd << " disconnected (EOF)\n";
              epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
              m_clients.erase(
                  std::remove(m_clients.begin(), m_clients.end(), fd),
                  m_clients.end());
              m_client_buffers.erase(fd);
              m_client_names.erase(fd);
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

  for (int fd : m_clients) {
    if (fd != sender_fd) {
      send(fd, packet.data(), packet.size(), 0);
    }
  }
}
