#ifndef CONNECTION_H
#define CONNECTION_H

#include <ctime>
#include <functional>
#include <memory>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "Buffer.h"
#include <mutex>

class EventLoop;
class Channel;

class Connection {
public:
  int fd;
  Buffer input_buffer;
  Buffer output_buffer;
  std::string username;
  time_t last_active;
  std::unique_ptr<Channel> channel;

  using MessageCallback = std::function<void(Connection *)>;
  using CloseCallback = std::function<void(Connection *)>;

  Connection(EventLoop *loop, int fd);
  ~Connection();

  ssize_t read_data();
  void write_data(const char *data, size_t len);
  void handleWrite();
  void close();

  void setReadCallback(MessageCallback cb) {
    m_messageCallback = std::move(cb);
  }
  void setCloseCallback(CloseCallback cb) { m_closeCallback = std::move(cb); }

  void handleRead();
  void handleClose();

  std::mutex out_mutex;

private:
  MessageCallback m_messageCallback;
  CloseCallback m_closeCallback;
};

#endif // CONNECTION_H
