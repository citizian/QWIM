#ifndef SERVER_H
#define SERVER_H

#include "json.hpp"
#include <ctime>
#include <netinet/in.h>
#include <string>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

class Server {
public:
  Server(int port);
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
};

#endif // SERVER_H
