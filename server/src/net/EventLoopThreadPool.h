#ifndef EVENT_LOOP_THREAD_POOL_H
#define EVENT_LOOP_THREAD_POOL_H

#include "EventLoopThread.h"
#include <memory>
#include <vector>

class EventLoop;

class EventLoopThreadPool {
public:
  EventLoopThreadPool(EventLoop *baseLoop);
  ~EventLoopThreadPool();

  void setThreadNum(int numThreads) { m_numThreads = numThreads; }
  void start();

  // Returns the next sub-reactor in round-robin fashion
  EventLoop *getNextLoop();

private:
  EventLoop *m_baseLoop;
  bool m_started;
  int m_numThreads;
  int m_next;
  std::vector<std::unique_ptr<EventLoopThread>> m_threads;
  std::vector<EventLoop *> m_loops;
};

#endif
