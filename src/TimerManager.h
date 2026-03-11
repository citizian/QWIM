#ifndef TIMERMANAGER_H
#define TIMERMANAGER_H

#include <ctime>
#include <queue>
#include <unordered_map>
#include <vector>

struct Timer {
  int fd;
  time_t expireTime;

  bool operator>(const Timer &other) const {
    return expireTime > other.expireTime;
  }
};

class TimerManager {
public:
  void addTimer(int fd, time_t expireTime);
  void removeTimer(int fd);
  std::vector<int> checkTimeout(time_t now);

private:
  std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> m_timers;
  std::unordered_map<int, time_t> m_expected_expire;
};

#endif // TIMERMANAGER_H
