#include "EventLoopThreadPool.h"
#include "EventLoop.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop)
    : m_baseLoop(baseLoop), m_started(false), m_numThreads(0), m_next(0) {}

EventLoopThreadPool::~EventLoopThreadPool() {
  // Sub-loops will be stopped implicitly when unique_ptrs to EventLoopThreads
  // are destroyed
}

void EventLoopThreadPool::start() {
  m_started = true;

  for (int i = 0; i < m_numThreads; ++i) {
    auto t = std::make_unique<EventLoopThread>();
    m_loops.push_back(t->startLoop());
    m_threads.push_back(std::move(t));
  }
}

EventLoop *EventLoopThreadPool::getNextLoop() {
  EventLoop *loop = m_baseLoop;

  if (!m_loops.empty()) {
    loop = m_loops[m_next];
    m_next++;
    if (static_cast<size_t>(m_next) >= m_loops.size()) {
      m_next = 0;
    }
  }
  return loop;
}
