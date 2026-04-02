#include "TimerManager.h"
#include "Connection.h"

void TimerManager::addTimer(std::weak_ptr<Connection> weak_conn, time_t expireTime) {
  if (auto conn = weak_conn.lock()) {
    m_timers.push({weak_conn, expireTime});
    m_expected_expire[conn->fd] = expireTime;
  }
}

void TimerManager::removeTimer(int fd) {
  // Lazy deletion: remove expected expire time so when the timer pops,
  // we know it's stale and ignore it.
  m_expected_expire.erase(fd);
}

std::vector<std::shared_ptr<Connection>> TimerManager::checkTimeout(time_t now) {
  std::vector<std::shared_ptr<Connection>> expired;

  while (!m_timers.empty()) {
    Timer node = m_timers.top();

    // Priority queue is sorted by expireTime asc, so if the earliest is in the
    // future, we stop.
    if (node.expireTime > now) {
      break;
    }

    m_timers.pop();

    // The weak_ptr could be invalid if the connection closed early manually
    if (auto conn = node.conn.lock()) {
      auto it = m_expected_expire.find(conn->fd);
      if (it != m_expected_expire.end() && it->second == node.expireTime) {
        // Valid timeout
        expired.push_back(conn);
        m_expected_expire.erase(it);
      }
    }
  }

  return expired;
}
