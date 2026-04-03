#include "MySQLPool.h"
#include "Logger.h"

MySQLPool& MySQLPool::instance() {
    static MySQLPool pool;
    return pool;
}

MySQLPool::~MySQLPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        MYSQL* conn = pool_.front();
        pool_.pop();
        mysql_close(conn);
    }
}

MYSQL* MySQLPool::createConnection() {
    MYSQL* conn = mysql_init(nullptr);
    if (conn == nullptr) {
        LOG_ERROR << "MySQL init error";
        return nullptr;
    }
    
    // Set character set
    mysql_options(conn, MYSQL_SET_CHARSET_NAME, "utf8mb4");

    conn = mysql_real_connect(conn, host_.c_str(), user_.c_str(), 
                              password_.c_str(), dbName_.c_str(), 
                              port_, nullptr, 0);

    if (conn == nullptr) {
        LOG_ERROR << "MySQL connection error: " << mysql_error(conn);
    }
    return conn;
}

bool MySQLPool::init(const std::string& host, const std::string& user, 
                     const std::string& password, const std::string& dbName, 
                     int port, int initialSize, int maxSize) {
    host_ = host;
    user_ = user;
    password_ = password;
    dbName_ = dbName;
    port_ = port;
    maxSize_ = maxSize;
    currentSize_ = 0;

    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < initialSize; ++i) {
        MYSQL* conn = createConnection();
        if (conn) {
            pool_.push(conn);
            currentSize_++;
        } else {
            LOG_ERROR << "Failed to allocate initial database connection.";
            return false; // Fail early if we can't create initial connections
        }
    }
    return true;
}

MYSQL* MySQLPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    while (pool_.empty()) {
        if (currentSize_ < maxSize_) {
            MYSQL* conn = createConnection();
            if (conn) {
                currentSize_++;
                return conn;
            } else {
                LOG_ERROR << "Pool reached thread limit or DB is refusing new connections.";
                return nullptr;
            }
        }
        // Wait until someone releases a connection
        cond_.wait(lock);
    }
    
    MYSQL* conn = pool_.front();
    pool_.pop();
    
    // Check connection validity using mysql_ping
    if (mysql_ping(conn) != 0) {
        mysql_close(conn);
        conn = createConnection();
        if (!conn) {
            currentSize_--; // we lost one connection and failed to recreate it
            return nullptr;
        }
    }

    return conn;
}

void MySQLPool::releaseConnection(MYSQL* conn) {
    if (!conn) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cond_.notify_one();
}
