#include "json.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

std::atomic<bool> running{true};

void receiveMessages(int sock) {
  while (running) {
    uint32_t recv_net_len = 0;
    ssize_t bytes_read = read(sock, &recv_net_len, 4);

    if (bytes_read == 4) {
      uint32_t expected_len = ntohl(recv_net_len);
      std::string response(expected_len, '\0');

      size_t total_read = 0;
      while (total_read < expected_len && running) {
        ssize_t r =
            read(sock, &response[total_read], expected_len - total_read);
        if (r > 0) {
          total_read += r;
        } else if (r == 0) {
          if (running)
            std::cout << "\nServer disconnected\n";
          running = false;
          return;
        } else {
          if (running)
            std::cerr << "\nRead error\n";
          running = false;
          return;
        }
      }

      if (!running)
        break;

      try {
        nlohmann::json j = nlohmann::json::parse(response);
        if (j.value("type", "") == "private") {
          std::cout << "\r[private] " << j.value("user", "Unknown") << ": "
                    << j.value("msg", "") << "\n";
          // Reprint the prompt dynamically
          std::cout << "> ";
          std::cout.flush();
        } else if (j.value("type", "") == "system") {
          std::cout << "\r[System]: " << j.value("msg", "") << "\n";
          std::cout << "> ";
          std::cout.flush();
        } else if (j.value("type", "") == "list") {
          std::cout << "\r--- Online Users ---\n";
          for (const auto &u : j["users"]) {
            std::cout << "- " << u.get<std::string>() << "\n";
          }
          std::cout << "--------------------\n";
          std::cout << "> ";
          std::cout.flush();
        } else if (j.value("type", "") == "chat") {
          std::cout << "\r[all] " << j.value("user", "Unknown") << ": "
                    << j.value("msg", "") << "\n";
          std::cout << "> ";
          std::cout.flush();
        } else if (j.value("type", "") == "history") {
          std::cout << "\r[History] " << j.value("msg", "") << "\n";
          std::cout << "> ";
          std::cout.flush();
        }
      } catch (const nlohmann::json::parse_error &e) {
        std::cerr << "\nJSON parse error: " << e.what() << "\n";
      }
    } else if (bytes_read == 0) {
      if (running)
        std::cout << "\nServer disconnected\n";
      running = false;
      break;
    } else {
      if (running)
        std::cerr << "\nRead error\n";
      running = false;
      break;
    }
  }
}

void sendHeartbeats(int sock) {
  while (running) {
    nlohmann::json hb_json;
    hb_json["type"] = "heartbeat";
    std::string hb_str = hb_json.dump();

    uint32_t len = hb_str.length();
    uint32_t net_len = htonl(len);
    std::vector<char> packet(4 + len);
    memcpy(packet.data(), &net_len, 4);
    memcpy(packet.data() + 4, hb_str.c_str(), len);

    send(sock, packet.data(), packet.size(), 0);
    std::this_thread::sleep_for(std::chrono::seconds(20));
  }
}

int main() {
  int sock = 0;
  struct sockaddr_in serv_addr;

  // 创建 socket
  if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
    std::cerr << "Socket creation error\n";
    return -1;
  }

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(8081);

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

  // Start the background receive threads
  std::thread recv_thread(receiveMessages, sock);
  std::thread hb_thread(sendHeartbeats, sock);

  std::cout << "Chat session started. Type /help for commands, or simply type "
               "to chat with everyone.\n";

  std::string input;
  while (running) {
    // 获取用户输入
    std::cout << "> ";
    if (!std::getline(std::cin, input)) {
      break;
    }

    if (input.empty())
      continue;

    nlohmann::json chat_json;

    if (input[0] == '/') {
      if (input == "/quit" || input == "/exit") {
        running = false;
        break;
      } else if (input == "/list") {
        chat_json["type"] = "list";
      } else if (input == "/help") {
        std::cout << "--- Commands ---\n"
                  << "/help - Show this menu\n"
                  << "/list - List online users\n"
                  << "/pm <user> <msg> - Send a private message\n"
                  << "/quit or /exit - Leave the chat\n"
                  << "<text> - Send a message to everyone (group chat)\n"
                  << "----------------\n";
        continue;
      } else if (input.find("/pm ") == 0) {
        size_t space_pos = input.find(' ', 4);
        if (space_pos != std::string::npos && space_pos > 4) {
          std::string target_user = input.substr(4, space_pos - 4);
          std::string message = input.substr(space_pos + 1);
          chat_json["type"] = "private";
          chat_json["to"] = target_user;
          chat_json["msg"] = message;
        } else {
          std::cout << "Usage: /pm <user> <message>\n";
          continue;
        }
      } else {
        std::cout << "Unknown command. Type /help for a list of commands.\n";
        continue;
      }
    } else {
      // Normal group chat
      chat_json["type"] = "chat";
      chat_json["msg"] = input;
    }

    std::string chat_str = chat_json.dump();

    uint32_t len = chat_str.length();
    uint32_t net_len = htonl(len);
    std::vector<char> packet(4 + len);
    memcpy(packet.data(), &net_len, 4);
    memcpy(packet.data() + 4, chat_str.c_str(), len);

    if (send(sock, packet.data(), packet.size(), 0) < 0) {
      std::cerr << "Failed to send message\n";
      running = false;
      break;
    }
  }

  // Clean shutdown
  running = false;
  shutdown(sock, SHUT_RDWR); // unblock the read thread if it's waiting
  close(sock);

  if (recv_thread.joinable()) {
    recv_thread.join();
  }
  if (hb_thread.joinable()) {
    hb_thread.join();
  }

  return 0;
}
