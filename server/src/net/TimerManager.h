#ifndef TIMERMANAGER_H
#define TIMERMANAGER_H

#include <ctime>
#include <queue>
#include <unordered_map>
#include <memory>
class Connection;

struct Timer {
  std::weak_ptr<Connection> conn;
  time_t expireTime;

  bool operator>(const Timer &other) const {
    return expireTime > other.expireTime;
  }
};

class TimerManager {
public:
  void addTimer(std::weak_ptr<Connection> conn, time_t expireTime);
  // We can drop the specific explicit removeTimer by fd since weak_ptr checking handles stale ones automatically,
  // or we keep removeTimer if we want to aggressively invalidate.
  // Actually, let's keep it to allow manual invalidation, but use the weak_ptr or pointer address as identity.
  // But weak_ptr address comparison is tedious. The user wants TimerManager purely on weak_ptr, so fd isn't needed. 
  // Let's remove manual `removeTimer` because weak_ptr natively drops if the Connection is deleted!
  // Wait, if a client renews heartbeat, we just add a new Timer object. The old one pops later,
  // but we can check if the Connection's `last_active` is truly expired when it pops!
  // Let's keep `removeTimer` just passing std::weak_ptr or shared_ptr to invalidate it.
  void removeTimer(int fd); // We will remove this in favor of just checking expiration on pop.
  
  std::vector<std::shared_ptr<Connection>> checkTimeout(time_t now);

private:
  std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> m_timers;
  // If we want to strictly manage by fd locally, we can keep m_expected_expire mapped by fd
  std::unordered_map<int, time_t> m_expected_expire;
};

#endif // TIMERMANAGER_H
