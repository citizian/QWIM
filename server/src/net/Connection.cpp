#include "Connection.h"
#include "Channel.h"
#include "EventLoop.h"
#include <iostream>

Connection::Connection(EventLoop *loop, int fd)
    : fd(fd), last_active(time(nullptr)),
      channel(std::make_unique<Channel>(loop, fd)) {
  channel->setReadCallback(std::bind(&Connection::handleRead, this));
  channel->setWriteCallback(std::bind(&Connection::handleWrite, this));
  channel->setCloseCallback(std::bind(&Connection::handleClose, this));
  channel->enableReading();
}

Connection::~Connection() { close(); }

void Connection::close() {
  if (fd != -1) {
    ::close(fd);
    fd = -1;
  }
}

void Connection::handleRead() {
  bool closed = false;
  bool error = false;
  while (true) {
    ssize_t bytes_read = read_data();
    if (bytes_read > 0) {
      // Keep reading
    } else if (bytes_read == 0) {
      closed = true;
      break;
    } else if (bytes_read == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break; // expected behavior in ET mode
      } else {
        error = true;
        break;
      }
    }
  }

  // Parse remaining payload before destruction
  if (input_buffer.readableBytes() > 0 && m_messageCallback) {
    m_messageCallback(shared_from_this());
  }

  // Handle the disconnect downstream if needed
  if (closed || error) {
    handleClose();
  }
}

void Connection::handleClose() {
  if (m_closeCallback) {
    m_closeCallback(shared_from_this());
  }
}

ssize_t Connection::read_data() {
  char buffer[1024] = {0};
  ssize_t bytes_read = read(fd, buffer, sizeof(buffer));
  if (bytes_read > 0) {
    input_buffer.append(buffer, bytes_read);
  }
  return bytes_read;
}

void Connection::write_data(const char *data, size_t len) {
  std::unique_lock<std::mutex> lock(out_mutex);
  output_buffer.append(data, len);
  if (!channel->isWriting()) {
    channel->enableWriting();
  }
}

void Connection::handleWrite() {
  std::unique_lock<std::mutex> lock(out_mutex);
  if (output_buffer.readableBytes() == 0) {
    if (channel->isWriting()) {
      channel->disableWriting();
    }
    return;
  }

  // Send in a loop to handle large buffers and avoid SIGPIPE by using MSG_NOSIGNAL.
  while (output_buffer.readableBytes() > 0) {
    // Limit single send size to avoid surprising large requests.
    size_t to_send = output_buffer.readableBytes();
    if (to_send > 64 * 1024) to_send = 64 * 1024;

    ssize_t bytes_written =
        ::send(fd, output_buffer.peek(), to_send, MSG_NOSIGNAL);

    if (bytes_written > 0) {
      output_buffer.retrieve(static_cast<size_t>(bytes_written));
      continue; // try to flush remaining data
    }

    if (bytes_written == 0) {
      // Nothing written; treat as would-block
      break;
    }

    // bytes_written < 0
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      // Can't write now, wait for next writable event
      break;
    }

    // Other errors (including EPIPE) - close connection
    lock.unlock(); // Prevent deadlock since handleClose may call back into server
    handleClose();
    return;
  }

  if (output_buffer.readableBytes() == 0) {
    if (channel->isWriting()) channel->disableWriting();
  }
}
