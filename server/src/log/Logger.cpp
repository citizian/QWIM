#include "Logger.h"
#include <cstring>
#include <sstream>
#include <sys/time.h>
#include <thread>

LogLevel g_logLevel = INFO;

const char* LogLevelName[NUM_LOG_LEVELS] = {
    "[TRACE] ",
    "[DEBUG] ",
    "[INFO]  ",
    "[WARN]  ",
    "[ERROR] ",
    "[FATAL] ",
};

void defaultOutput(const char* msg, int len) {
    fwrite(msg, 1, len, stdout);
}

void defaultFlush() {
    fflush(stdout);
}

Logger::OutputFunc g_output = defaultOutput;
Logger::FlushFunc g_flush = defaultFlush;

Logger::Impl::Impl(LogLevel level, int savedErrno, const char* file, int line)
  : level_(level),
    line_(line),
    basename_(file) {
    const char* slash = strrchr(file, '/');
    if (slash) {
        basename_ = slash + 1;
    }
    formatTime();
    std::stringstream ss;
    ss << std::this_thread::get_id();
    stream_ << "[" << ss.str() << "] ";
    stream_ << LogLevelName[level];
}

void Logger::Impl::formatTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t seconds = tv.tv_sec;
    int microseconds = tv.tv_usec;
    struct tm tm_time;
    localtime_r(&seconds, &tm_time);

    char buf[64] = {0};
    snprintf(buf, sizeof(buf), "[%4d-%02d-%02d %02d:%02d:%02d.%06d] ",
             tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
             tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
             microseconds);
    stream_ << buf;
}

void Logger::Impl::finish() {
    stream_ << " - " << basename_ << ":" << line_ << '\n';
}

Logger::Logger(const char* file, int line, LogLevel level, const char* func)
  : impl_(level, 0, file, line) {
    impl_.stream_ << func << ' ';
}

Logger::Logger(const char* file, int line, LogLevel level)
  : impl_(level, 0, file, line) {
}

Logger::~Logger() {
    impl_.finish();
    const LogStream::Buffer& buf(stream().buffer());
    g_output(buf.data(), buf.length());
    if (impl_.level_ == FATAL) {
        g_flush();
        abort();
    }
}

void Logger::setLogLevel(LogLevel level) {
    g_logLevel = level;
}

void Logger::setOutput(OutputFunc out) {
    g_output = out;
}

void Logger::setFlush(FlushFunc flush) {
    g_flush = flush;
}
