#pragma once

#include "Connection.h"
#include "json.hpp"
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

class IMServer;

class ChatService {
public:
    static ChatService& instance();

    void init();

    void onDisconnect(std::shared_ptr<Connection> conn);

    void handleLogin(std::shared_ptr<Connection> conn, const nlohmann::json& payload, IMServer* server);
    void handleChat(std::shared_ptr<Connection> conn, const nlohmann::json& payload, IMServer* server);
    void handlePrivate(std::shared_ptr<Connection> conn, const nlohmann::json& payload, IMServer* server);
    void handleList(std::shared_ptr<Connection> conn, const nlohmann::json& payload, IMServer* server);
    void handleHeartbeat(std::shared_ptr<Connection> conn, const nlohmann::json& payload, IMServer* server);

private:
    ChatService() = default;
    ~ChatService() = default;
    ChatService(const ChatService&) = delete;
    ChatService& operator=(const ChatService&) = delete;

    std::mutex mutex_;
    std::unordered_map<std::string, int> online_users_;
    std::deque<std::string> message_history_;
};
