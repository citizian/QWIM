#include "../server/include/json.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

int main() {
  int sock = 0;
  struct sockaddr_in serv_addr;
  char buffer[1024] = {0};

  // 创建 socket
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    std::cerr << "Socket creation error\n";
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(8080);

  // 将 IP 地址转换为二进制格式
  if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
    std::cerr << "Invalid address / Address not supported\n";
    return -1;
  }

  // 连接到服务器
  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    std::cerr << "Connection Failed\n";
    return -1;
  }

  std::cout << "Connected to server.\n";

  std::string username;
  std::cout << "Enter your username: ";
  std::getline(std::cin, username);

  // 发送 login 消息
  nlohmann::json login_json;
  login_json["type"] = "login";
  login_json["user"] = username;
  std::string login_str = login_json.dump();

  uint32_t login_len = login_str.length();
  uint32_t login_net_len = htonl(login_len);
  std::vector<char> login_packet(4 + login_len);
  memcpy(login_packet.data(), &login_net_len, 4);
  memcpy(login_packet.data() + 4, login_str.c_str(), login_len);

  if (send(sock, login_packet.data(), login_packet.size(), 0) < 0) {
    std::cerr << "Failed to send login message\n";
    return -1;
  }

  std::cout << "Type messages to chat or 'quit' to exit.\n";

  std::string message;
  while (true) {
    // 获取用户输入
    std::cout << "> ";
    if (!std::getline(std::cin, message)) {
      break; // 遇到EOF退出
    }

    if (message == "quit" || message == "exit") {
      break;
    }

    // 发送 chat 消息
    nlohmann::json chat_json;
    chat_json["type"] = "chat";
    chat_json["msg"] = message;
    std::string chat_str = chat_json.dump();

    uint32_t len = chat_str.length();
    uint32_t net_len = htonl(len);
    std::vector<char> packet(4 + len);
    memcpy(packet.data(), &net_len, 4);
    memcpy(packet.data() + 4, chat_str.c_str(), len);

    if (send(sock, packet.data(), packet.size(), 0) < 0) {
      std::cerr << "Failed to send message\n";
      break;
    }

    // 接收服务器的回应 (由于客户端是阻塞模式，直接读取长度然后读取内容)
    uint32_t recv_net_len = 0;
    ssize_t bytes_read = read(sock, &recv_net_len, 4);
    if (bytes_read == 4) {
      uint32_t expected_len = ntohl(recv_net_len);
      std::string response(expected_len, '\0');

      size_t total_read = 0;
      while (total_read < expected_len) {
        ssize_t r =
            read(sock, &response[total_read], expected_len - total_read);
        if (r > 0) {
          total_read += r;
        } else if (r == 0) {
          std::cout << "Server disconnected\n";
          close(sock);
          return 0;
        } else {
          std::cerr << "Read error\n";
          close(sock);
          return -1;
        }
      }

      try {
        nlohmann::json j = nlohmann::json::parse(response);
        if (j.value("type", "") == "chat") {
          std::cout << "\r[" << j.value("user", "Unknown")
                    << "]: " << j.value("msg", "") << "\n> ";
          std::cout.flush();
        }
      } catch (const nlohmann::json::parse_error &e) {
        std::cerr << "JSON parse error: " << e.what() << "\n";
      }
    } else if (bytes_read == 0) {
      std::cout << "Server disconnected\n";
      break;
    } else {
      std::cerr << "Read error\n";
      break;
    }
  }

  // 关闭 socket
  close(sock);

  return 0;
}
