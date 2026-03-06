#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

int main() {
    // 创建 socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    // 设置服务器地址和端口
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8080);

    // 绑定 socket 到地址
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        std::cerr << "Failed to bind socket\n";
        close(server_fd);
        return 1;
    }

    // 监听连接
    if (listen(server_fd, 5) == -1) {
        std::cerr << "Failed to listen\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Server is listening on port 8080...\n";

    while (true) {
        // 接受客户端连接
        sockaddr_in client_address{};
        socklen_t client_len = sizeof(client_address);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
        
        if (client_fd == -1) {
            std::cerr << "Failed to accept connection\n";
            continue;
        }

        // 接收消息
        char buffer[1024] = {0};
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        
        if (bytes_read > 0) {
            std::cout << "Received message: " << buffer << std::endl;
        }

        // 关闭客户端连接
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
