# Logging 模块说明

## 概述

本模块提供轻量且高效的日志子系统，包含三部分：

- `LogStream`：格式化与缓冲的流式接口（内存缓冲，非线程安全）。
- `Logger`：基于 RAII 的日志条目构建与输出，支持日志等级与可替换的输出/刷新回调。
- `AsyncLogger`：异步文件写入的后台日志器，使用大缓冲与单线程写入以减少主线程阻塞。

此文档仅包含 API 与实现说明（不含示例代码或运行测试步骤）。

## 通用常量与概念

- `kSmallBuffer = 4000`：`LogStream` 使用的小缓冲（约 4 KB）。
- `kLargeBuffer = 4000 * 1000`：`AsyncLogger` 使用的大缓冲（约 4 MB）。
- `kMaxNumericSize = 48`：数值到字符串格式化时的最大缓冲长度。
- 日志等级：`TRACE, DEBUG, INFO, WARN, ERROR, FATAL`（由 `LogLevel` 枚举定义）。

## LogStream（文件：src/LogStream.h / src/LogStream.cpp）

职责：提供高效的 `<<` 格式化接口，将各种类型（整数、浮点、指针、字符串等）追加到固定大小内存缓冲中，用于构建日志消息。

主要类型与接口：

- `FixedBuffer<SIZE>`：模板固定缓冲，提供 `append`, `current`, `avail`, `reset`, `bzero`。
- `LogStream::Buffer`：等同于 `FixedBuffer<kSmallBuffer>`。
- `LogStream& operator<<(...)`：支持整数、浮点、指针、`const char*`、`std::string`、`std::string_view` 等重载。
- `void append(const char* data, int len)`：直接追加原始数据。
- `const Buffer& buffer() const`：获取缓冲内容用于输出。
- `void resetBuffer()`：重置缓冲。

线程安全性：`LogStream` 本身非线程安全；通常每个 `Logger` 实例包含独立 `LogStream`，跨线程访问需外部同步。

注意事项：如果追加内容超过 `kSmallBuffer`，超出部分不会被追加（无错误返回），需要调用方注意消息大小。

## Logger（文件：src/Logger.h / src/Logger.cpp）

职责：基于 RAII 构建完整日志条目（时间戳、线程 id、日志等级、源文件与行号），并通过可替换的回调输出。

主要结构与接口：

- `Logger::Impl`：内部实现，负责时间格式化与持有 `LogStream`。
- 全局变量：`g_logLevel`（当前等级）、`g_output`（输出回调）、`g_flush`（刷新回调）。
- 构造：`Logger(const char* file, int line, LogLevel level[, const char* func])`。
- `LogStream& stream()`：获取用于追加日志的 `LogStream`。
- 静态 API：`setLogLevel`, `logLevel`, `setOutput`, `setFlush`。
- 宏：`LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `LOG_FATAL` —— 按等级条件创建临时 `Logger` 并在析构时输出。

行为细节：

- 析构时会调用 `g_output(buf.data(), buf.length())` 把格式化好的缓冲发送到输出回调。
- 当等级为 `FATAL` 时，会调用 `g_flush()` 并 `abort()` 进程。

线程与全局状态：

- `g_output`, `g_flush`, `g_logLevel` 为全局可写变量；若在运行时修改这些全局变量，应保证外部同步以避免竞态。
- 输出是否线程安全取决于 `g_output` 的实现；若指向线程安全函数（例如将数据交给线程安全的 `AsyncLogger`），则可以安全并发输出。

## AsyncLogger（文件：src/AsyncLogger.h / src/AsyncLogger.cpp）

职责：在后台线程中批量写入日志到磁盘文件，降低主线程在日志 I/O 上的阻塞，适用于高吞吐量场景。

主要设计点与接口：

- 构造：`AsyncLogger(std::string basename, off_t rollSize, int flushInterval = 3)`（当前实现未使用 `rollSize`）。
- `void append(const char* logline, int len)`：线程安全的追加接口，主线程调用。
- `void start()` / `void stop()`：启动/停止后台写线程。

内部实现细节：

- 使用 `FixedBuffer<kLargeBuffer>` 作为缓冲块，维护 `currentBuffer_`, `nextBuffer_` 与 `buffers_`（队列）。
- 使用 `std::mutex` 与 `std::condition_variable` 保护缓冲访问；通过 `std::atomic<bool> running_` 控制线程生命周期。
- 后台线程用 `fopen(basename_.c_str(), "ae")` 打开文件并使用 `fwrite` 写入，写入后 `fflush`。
- 当 `buffers_` 堆积超过阈值（>25）时，会丢弃中间大量缓冲并在 stderr 与文件中写入一条“Dropped log messages”提示。

注意与限制：

- `append` 在缓冲耗尽时选择丢弃或分配新缓冲；在极高写入速率下会丢弃日志以保护系统资源。
- `rollSize_` 参数预留给日志切割功能，但当前实现未实现该逻辑。
- 若 `fopen` 失败，当前实现会写 stderr 并返回（导致日志丢失）。

## 常见问题与建议

- 若替换 `g_output`，请确保新回调在并发场景下线程安全。
- 若不希望丢失日志，应改进 `AsyncLogger` 的策略（例如阻塞 `append`、增大队列上限或引入持久化队列）。
- 要实现日志切割，请在 `AsyncLogger` 写入路径中使用 `rollSize_` 检查并重命名/重开日志文件。

## 公共 API 快速索引

- `LogStream`
  - `LogStream& operator<<(...)`（多种类型）
  - `void append(const char* data, int len)`
  - `const Buffer& buffer() const`
  - `void resetBuffer()`

- `Logger`
  - `Logger(const char* file, int line, LogLevel level[, const char* func])`
  - `LogStream& stream()`
  - `static LogLevel logLevel()`
  - `static void setLogLevel(LogLevel level)`
  - `static void setOutput(Logger::OutputFunc)`
  - `static void setFlush(Logger::FlushFunc)`
  - 宏：`LOG_TRACE`, `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `LOG_FATAL`

- `AsyncLogger`
  - `AsyncLogger(std::string basename, off_t rollSize, int flushInterval = 3)`
  - `void append(const char* logline, int len)`
  - `void start()` / `void stop()`

## 结语

此文档为模块 API 与实现说明，适合放置于仓库以便维护者快速理解日志子系统的结构、约束与扩展点。如需加入示例、测试脚本或英文版，我可在后续提交中补充。
