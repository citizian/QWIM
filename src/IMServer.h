#ifndef SERVER_H
#define SERVER_H

#include "Channel.h"
#include "Connection.h"
#include "EventLoop.h"
#include "EventLoopThreadPool.h"
#include "TimerManager.h"
#include "json.hpp"

#include <ctime>
#include <deque>
#include <fstream>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <unordered_map>
#include <vector>

class IMServer {
public:
  IMServer(const std::string &config_file);
  ~IMServer();

  void start();

  void broadcastMessage(int sender_fd, const char *message, uint32_t len);
  bool isConnectionActive(int fd);
  void sendToUser(int target_fd, const char *data, size_t len);

private:
  void setNonBlocking(int fd);
  void removeClient(int fd);

  void handleNewConnection();
  void onConnectionMessage(std::shared_ptr<Connection> conn);
  void onConnectionClose(std::shared_ptr<Connection> conn);

  int m_server_fd;
  int m_port;
  bool m_running;

  std::unique_ptr<EventLoop> m_loop;
  std::unique_ptr<EventLoopThreadPool> m_thread_pool;
  std::unique_ptr<Channel> m_server_channel;
  std::unique_ptr<TimerManager> m_timer_manager;

  std::mutex m_mutex;

  std::unordered_map<int, std::shared_ptr<Connection>> m_connections;
};

#endif // SERVER_H
