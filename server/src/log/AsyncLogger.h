#pragma once
#include "LogStream.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AsyncLogger {
public:
    AsyncLogger(std::string basename, off_t rollSize, int flushInterval = 3);
    ~AsyncLogger();

    void append(const char* logline, int len);

    void start() {
        running_ = true;
        thread_ = std::thread(&AsyncLogger::threadFunc, this);
    }

    void stop() {
        running_ = false;
        cond_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void threadFunc();

    using Buffer = FixedBuffer<kLargeBuffer>;
    using BufferPtr = std::unique_ptr<Buffer>;
    using BufferVector = std::vector<BufferPtr>;

    std::string basename_;
    off_t rollSize_;
    const int flushInterval_;

    std::atomic<bool> running_;
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cond_;
    
    BufferPtr currentBuffer_;
    BufferPtr nextBuffer_;
    BufferVector buffers_;
};
