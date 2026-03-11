#include "EventLoopThread.h"
#include "EventLoop.h"
#include <functional>

EventLoopThread::EventLoopThread() : m_loop(nullptr), m_exiting(false) {}

EventLoopThread::~EventLoopThread() {
  m_exiting = true;
  if (m_loop != nullptr) {
    m_loop->quit(); // Signal the loop to stop
  }
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

EventLoop *EventLoopThread::startLoop() {
  m_thread = std::thread(std::bind(&EventLoopThread::threadFunc, this));

  EventLoop *loop = nullptr;
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cond.wait(lock, [this] { return m_loop != nullptr; });
    loop = m_loop;
  }
  return loop;
}

void EventLoopThread::threadFunc() {
  EventLoop loop;

  {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_loop = &loop;
    m_cond.notify_one();
  }

  loop.loop(); // Blocks here until loop.quit()

  std::lock_guard<std::mutex> lock(m_mutex);
  m_loop = nullptr;
}
