#pragma once
#include "LogStream.h"
#include <string>

enum LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
    NUM_LOG_LEVELS
};

class Logger {
public:
    Logger(const char* file, int line, LogLevel level, const char* func);
    Logger(const char* file, int line, LogLevel level);
    ~Logger();

    LogStream& stream() { return impl_.stream_; }

    static LogLevel logLevel();
    static void setLogLevel(LogLevel level);

    using OutputFunc = void (*)(const char* msg, int len);
    using FlushFunc = void (*)();
    static void setOutput(OutputFunc);
    static void setFlush(FlushFunc);

private:
    class Impl {
    public:
        Impl(LogLevel level, int savedErrno, const char* file, int line);
        void formatTime();
        void finish();

        LogLevel level_;
        int line_;
        std::string basename_;
        LogStream stream_;
    };
    Impl impl_;
};

extern LogLevel g_logLevel;

inline LogLevel Logger::logLevel() { return g_logLevel; }

#define LOG_TRACE if (Logger::logLevel() <= TRACE) \
    Logger(__FILE__, __LINE__, TRACE, __func__).stream()
#define LOG_DEBUG if (Logger::logLevel() <= DEBUG) \
    Logger(__FILE__, __LINE__, DEBUG, __func__).stream()
#define LOG_INFO if (Logger::logLevel() <= INFO) \
    Logger(__FILE__, __LINE__, INFO).stream()
#define LOG_WARN Logger(__FILE__, __LINE__, WARN).stream()
#define LOG_ERROR Logger(__FILE__, __LINE__, ERROR).stream()
#define LOG_FATAL Logger(__FILE__, __LINE__, FATAL).stream()
