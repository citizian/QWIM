#include "TimerManager.h"

void TimerManager::addTimer(int fd, time_t expireTime) {
  m_timers.push({fd, expireTime});
  m_expected_expire[fd] = expireTime;
}

void TimerManager::removeTimer(int fd) {
  // Lazy deletion: remove expected expire time so when the timer pops,
  // we know it's stale and ignore it.
  m_expected_expire.erase(fd);
}

std::vector<int> TimerManager::checkTimeout(time_t now) {
  std::vector<int> expired;

  while (!m_timers.empty()) {
    Timer node = m_timers.top();

    // Priority queue is sorted by expireTime asc, so if the earliest is in the
    // future, we stop.
    if (node.expireTime > now) {
      break;
    }

    m_timers.pop();

    // Validate if this timer is still active and hasn't been updated
    auto it = m_expected_expire.find(node.fd);
    if (it != m_expected_expire.end() && it->second == node.expireTime) {
      // Valid timeout
      expired.push_back(node.fd);
      m_expected_expire.erase(it);
    }
  }

  return expired;
}
