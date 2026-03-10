#ifndef BUFFER_H
#define BUFFER_H

#include <cstddef>
#include <string>
#include <vector>

class Buffer {
public:
  Buffer(size_t initial_size = 1024)
      : m_buffer(initial_size), m_read_index(0), m_write_index(0) {}

  void append(const char *data, size_t len) {
    ensureCapacity(len);
    std::copy(data, data + len, m_buffer.begin() + m_write_index);
    m_write_index += len;
  }

  void retrieve(size_t len) {
    if (len < readableBytes()) {
      m_read_index += len;
    } else {
      retrieveAll();
    }
  }

  void retrieveAll() {
    m_read_index = 0;
    m_write_index = 0;
  }

  const char *peek() const { return m_buffer.data() + m_read_index; }

  size_t readableBytes() const { return m_write_index - m_read_index; }

  std::string readAllAsString() {
    std::string result(peek(), readableBytes());
    retrieveAll();
    return result;
  }

  std::string readAsString(size_t len) {
    std::string result(peek(), len);
    retrieve(len);
    return result;
  }

private:
  void ensureCapacity(size_t len) {
    if (writableBytes() < len) {
      if (writableBytes() + prependableBytes() < len) {
        // Need to grow
        m_buffer.resize(m_write_index + len);
      } else {
        // Move readable data to front
        size_t readable = readableBytes();
        std::copy(m_buffer.begin() + m_read_index,
                  m_buffer.begin() + m_write_index, m_buffer.begin());
        m_read_index = 0;
        m_write_index = m_read_index + readable;
      }
    }
  }

  size_t writableBytes() const { return m_buffer.size() - m_write_index; }

  size_t prependableBytes() const { return m_read_index; }

  std::vector<char> m_buffer;
  size_t m_read_index;
  size_t m_write_index;
};

#endif // BUFFER_H
