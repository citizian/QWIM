#ifndef SERVER_H
#define SERVER_H

#include <netinet/in.h>
#include <sys/epoll.h>
#include <vector>

class Server {
public:
  Server(int port);
  ~Server();

  void start();

private:
  void broadcastMessage(int sender_fd, const char *message);
  void setNonBlocking(int fd);

  int m_server_fd;
  int m_port;
  bool m_running;

  int m_epoll_fd;
  std::vector<int> m_clients;
};

#endif // SERVER_H
