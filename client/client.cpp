#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

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

  std::cout << "Connected to server. Type 'quit' to exit.\n";

  std::string message;
  while (true) {
    // 获取用户输入
    std::cout << "Enter message to send: ";
    if (!std::getline(std::cin, message)) {
      break; // 遇到EOF退出
    }

    if (message == "quit" || message == "exit") {
      break;
    }

    // 发送消息
    if (send(sock, message.c_str(), message.length(), 0) < 0) {
      std::cerr << "Failed to send message\n";
      break;
    }

    // 接收服务器的回应
    memset(buffer, 0, sizeof(buffer)); // 清空 buffer
    ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
      std::cout << "Response from server: " << buffer << "\n";
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
