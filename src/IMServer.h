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

private:
  void broadcastMessage(int sender_fd, const char *message, uint32_t len);
  void setNonBlocking(int fd);
  void removeClient(int fd);

  void handleNewConnection();
  void onConnectionMessage(Connection *conn);
  void onConnectionClose(Connection *conn);

  int m_server_fd;
  int m_port;
  bool m_running;

  std::unique_ptr<EventLoop> m_loop;
  std::unique_ptr<EventLoopThreadPool> m_thread_pool;
  std::unique_ptr<Channel> m_server_channel;
  std::unique_ptr<TimerManager> m_timer_manager;

  std::mutex m_mutex;

  std::unordered_map<int, std::unique_ptr<Connection>> m_connections;
  std::unordered_map<std::string, int> m_online_users;
  std::deque<std::string> m_message_history;
  std::unordered_map<std::string, std::string> m_config;

  void loadConfig(const std::string &filename);
};

#endif // SERVER_H
