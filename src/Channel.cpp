#include "Channel.h"
#include "EventLoop.h"

Channel::Channel(EventLoop *loop, int fd)
    : m_loop(loop), m_fd(fd), m_events(0), m_revents(0), m_in_epoll(false) {}

Channel::~Channel() {
  // We can let EventLoop remove us, but normally Server removes the channel
  // before deletion.
}

void Channel::handleEvent() {
  if (m_revents & EPOLLIN) {
    if (m_readCallback)
      m_readCallback();
  }

  if (m_revents & EPOLLOUT) {
    if (m_writeCallback)
      m_writeCallback();
  }

  if (m_revents & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
    if (m_closeCallback)
      m_closeCallback();
  }
}

void Channel::enableReading() {
  m_events |= EPOLLIN | EPOLLET | EPOLLRDHUP;
  m_loop->updateChannel(this);
}

void Channel::enableWriting() {
  m_events |= EPOLLOUT;
  m_loop->updateChannel(this);
}

void Channel::disableWriting() {
  m_events &= ~EPOLLOUT;
  m_loop->updateChannel(this);
}
