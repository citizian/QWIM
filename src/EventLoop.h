#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <functional>
#include <sys/epoll.h>

class Channel;

class EventLoop {
public:
  EventLoop();
  ~EventLoop();

  void loop();
  void quit();

  void updateChannel(Channel *channel);
  void removeChannel(Channel *channel);

  void setTickCallback(std::function<void()> cb) {
    m_tickCallback = std::move(cb);
  }

private:
  int m_epoll_fd;
  bool m_quit;
  std::function<void()> m_tickCallback;
};

#endif // EVENTLOOP_H
