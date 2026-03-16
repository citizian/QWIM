#pragma once
#include <string>
#include <string_view>
#include <cstring>

const int kSmallBuffer = 4000;
const int kLargeBuffer = 4000 * 1000;

template<int SIZE>
class FixedBuffer {
public:
    FixedBuffer() : cur_(data_) {}
    ~FixedBuffer() {}

    void append(const char* buf, size_t len) {
        if (static_cast<size_t>(avail()) > len) {
            memcpy(cur_, buf, len);
            cur_ += len;
        }
    }

    const char* data() const { return data_; }
    int length() const { return static_cast<int>(cur_ - data_); }
    char* current() { return cur_; }
    int avail() const { return static_cast<int>(end() - cur_); }
    void add(size_t len) { cur_ += len; }
    void reset() { cur_ = data_; }
    void bzero() { memset(data_, 0, sizeof(data_)); }

private:
    const char* end() const { return data_ + sizeof(data_); }
    char data_[SIZE];
    char* cur_;
};

class LogStream {
public:
    using Buffer = FixedBuffer<kSmallBuffer>;

    LogStream& operator<<(short);
    LogStream& operator<<(unsigned short);
    LogStream& operator<<(int);
    LogStream& operator<<(unsigned int);
    LogStream& operator<<(long);
    LogStream& operator<<(unsigned long);
    LogStream& operator<<(long long);
    LogStream& operator<<(unsigned long long);

    LogStream& operator<<(const void*);
    
    LogStream& operator<<(float v) {
        *this << static_cast<double>(v);
        return *this;
    }
    LogStream& operator<<(double);
    
    LogStream& operator<<(char v) {
        append(&v, 1);
        return *this;
    }

    LogStream& operator<<(const char* str) {
        if (str) {
            append(str, strlen(str));
        } else {
            append("(null)", 6);
        }
        return *this;
    }

    LogStream& operator<<(const unsigned char* str) {
        return operator<<(reinterpret_cast<const char*>(str));
    }

    LogStream& operator<<(const std::string& v) {
        append(v.c_str(), v.size());
        return *this;
    }

    LogStream& operator<<(std::string_view v) {
        append(v.data(), v.size());
        return *this;
    }

    void append(const char* data, int len) { buffer_.append(data, len); }
    const Buffer& buffer() const { return buffer_; }
    void resetBuffer() { buffer_.reset(); }

private:
    template<typename T>
    void formatInteger(T);

    Buffer buffer_;
    static const int kMaxNumericSize = 48;
};
