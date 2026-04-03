#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <mysql/mysql.h>
#include <queue>
#include <string>

class MySQLPool {
public:
  static MySQLPool &instance();

  // Initialize the pool
  bool init(const std::string &host, const std::string &user,
            const std::string &password, const std::string &dbName, int port,
            int initialSize, int maxSize);

  // Get a connection from the pool
  MYSQL *getConnection();

  // Return a connection back to the pool
  void releaseConnection(MYSQL *conn);

private:
  MySQLPool() = default;
  ~MySQLPool();
  MySQLPool(const MySQLPool &) = delete;
  MySQLPool &operator=(const MySQLPool &) = delete;

  MYSQL *createConnection();

  std::string host_;
  std::string user_;
  std::string password_;
  std::string dbName_;
  int port_;

  int maxSize_;
  int currentSize_;

  std::queue<MYSQL *> pool_;
  std::mutex mutex_;
  std::condition_variable cond_;
};

// RAII wrapper for safe connection release
class MySQLConnectionGuard {
public:
  MySQLConnectionGuard(MySQLPool *pool) : pool_(pool) {
    conn_ = pool_->getConnection();
  }
  ~MySQLConnectionGuard() {
    if (conn_) {
      pool_->releaseConnection(conn_);
    }
  }
  MYSQL *get() const { return conn_; }

private:
  MYSQL *conn_;
  MySQLPool *pool_;
};
