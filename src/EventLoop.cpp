#include "EventLoop.h"
#include "Channel.h"
#include <iostream>
#include <unistd.h>

EventLoop::EventLoop() : m_epoll_fd(epoll_create1(0)), m_quit(false) {
  if (m_epoll_fd == -1) {
    std::cerr << "Failed to create epoll fd in EventLoop.\n";
    exit(1);
  }
}

EventLoop::~EventLoop() { close(m_epoll_fd); }

void EventLoop::loop() {
  m_quit = false;
  const int MAX_EVENTS = 10;
  epoll_event events[MAX_EVENTS];

  while (!m_quit) {
    int nfds = epoll_wait(m_epoll_fd, events, MAX_EVENTS, 1000);
    if (nfds == -1) {
      if (errno == EINTR)
        continue;
      std::cerr << "epoll_wait error in EventLoop.\n";
      break;
    }

    for (int i = 0; i < nfds; ++i) {
      Channel *channel = static_cast<Channel *>(events[i].data.ptr);
      channel->setRevents(events[i].events);
      channel->handleEvent();
    }

    // Call tick callback for things like heartbeat scanning
    if (m_tickCallback) {
      m_tickCallback();
    }
  }
}

void EventLoop::quit() { m_quit = true; }

void EventLoop::updateChannel(Channel *channel) {
  int fd = channel->getFd();
  struct epoll_event event;
  event.events = channel->getEvents();
  event.data.ptr = channel;

  if (!channel->isInEpoll()) {
    epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &event);
    channel->setInEpoll(true);
  } else {
    epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &event);
  }
}

void EventLoop::removeChannel(Channel *channel) {
  if (channel->isInEpoll()) {
    epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, channel->getFd(), nullptr);
    channel->setInEpoll(false);
  }
}
