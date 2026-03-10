#ifndef CHANNEL_H
#define CHANNEL_H

#include <functional>
#include <sys/epoll.h>

class EventLoop;

class Channel {
public:
  using EventCallback = std::function<void()>;

  Channel(EventLoop *loop, int fd);
  ~Channel();

  void handleEvent();
  void enableReading();
  void enableWriting();
  void disableWriting();
  bool isWriting() const { return m_events & EPOLLOUT; }

  int getFd() const { return m_fd; }
  uint32_t getEvents() const { return m_events; }
  uint32_t getRevents() const { return m_revents; }
  void setRevents(uint32_t rev) { m_revents = rev; }

  bool isInEpoll() const { return m_in_epoll; }
  void setInEpoll(bool in) { m_in_epoll = in; }

  void setReadCallback(EventCallback cb) { m_readCallback = std::move(cb); }
  void setWriteCallback(EventCallback cb) { m_writeCallback = std::move(cb); }
  void setCloseCallback(EventCallback cb) { m_closeCallback = std::move(cb); }

private:
  EventLoop *m_loop;
  int m_fd;
  uint32_t m_events;
  uint32_t m_revents;
  bool m_in_epoll;

  EventCallback m_readCallback;
  EventCallback m_writeCallback;
  EventCallback m_closeCallback;
};

#endif // CHANNEL_H
