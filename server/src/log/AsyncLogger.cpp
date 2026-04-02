#include "AsyncLogger.h"
#include <chrono>
#include <cstdio>
#include <iostream>

#include <filesystem>
#include <system_error>

AsyncLogger::AsyncLogger(std::string basename, off_t rollSize, int flushInterval)
    : basename_(basename),
      rollSize_(rollSize),
      flushInterval_(flushInterval),
      running_(false),
      currentBuffer_(new Buffer),
      nextBuffer_(new Buffer) {
    currentBuffer_->bzero();
    nextBuffer_->bzero();
}

AsyncLogger::~AsyncLogger() {
    if (running_) {
        stop();
    }
}

void AsyncLogger::append(const char* logline, int len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (currentBuffer_->avail() > len) {
        currentBuffer_->append(logline, len);
    } else {
        buffers_.push_back(std::move(currentBuffer_));
        if (nextBuffer_) {
            currentBuffer_ = std::move(nextBuffer_);
        } else {
            currentBuffer_.reset(new Buffer); 
        }
        currentBuffer_->append(logline, len);
        cond_.notify_one();
    }
}

void AsyncLogger::threadFunc() {
    BufferPtr buffer1(new Buffer);
    BufferPtr buffer2(new Buffer);
    buffer1->bzero();
    buffer2->bzero();
    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    std::filesystem::path logPath(basename_);
    if (logPath.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(logPath.parent_path(), ec);
    }
    FILE* fp = fopen(basename_.c_str(), "ae"); 
    if (!fp) {
        std::cerr << "AsyncLogger fail to open file " << basename_ << std::endl;
        return;
    }

    while (running_) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (buffers_.empty()) {
                cond_.wait_for(lock, std::chrono::seconds(flushInterval_));
            }
            buffers_.push_back(std::move(currentBuffer_));
            currentBuffer_ = std::move(buffer1);
            buffersToWrite.swap(buffers_);
            if (!nextBuffer_) {
                nextBuffer_ = std::move(buffer2);
            }
        }

        if (buffersToWrite.size() > 25) {
            char buf[256];
            snprintf(buf, sizeof(buf), "Dropped log messages, %zu larger than 25\n", buffersToWrite.size());
            fputs(buf, stderr);
            fputs(buf, fp);
            buffersToWrite.erase(buffersToWrite.begin() + 2, buffersToWrite.end());
        }

        for (const auto& buffer : buffersToWrite) {
            fwrite(buffer->data(), 1, buffer->length(), fp);
        }

        if (buffersToWrite.size() > 2) {
            buffersToWrite.resize(2);
        }

        if (!buffer1) {
            buffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            buffer1->reset();
        }

        if (!buffer2) {
            buffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            buffer2->reset();
        }

        buffersToWrite.clear();
        fflush(fp);
    }

    fflush(fp);
    fclose(fp);
}
