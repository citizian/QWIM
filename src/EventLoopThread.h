#ifndef EVENT_LOOP_THREAD_H
#define EVENT_LOOP_THREAD_H

#include <condition_variable>
#include <mutex>
#include <thread>

class EventLoop;

class EventLoopThread {
public:
  EventLoopThread();
  ~EventLoopThread();

  EventLoop *startLoop();

private:
  void threadFunc();

  EventLoop *m_loop;
  bool m_exiting;
  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_cond;
};

#endif
