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

    void onDisconnect(Connection* conn);

    void handleLogin(Connection* conn, const nlohmann::json& payload, IMServer* server);
    void handleChat(Connection* conn, const nlohmann::json& payload, IMServer* server);
    void handlePrivate(Connection* conn, const nlohmann::json& payload, IMServer* server);
    void handleList(Connection* conn, const nlohmann::json& payload, IMServer* server);
    void handleHeartbeat(Connection* conn, const nlohmann::json& payload, IMServer* server);

private:
    ChatService() = default;
    ~ChatService() = default;
    ChatService(const ChatService&) = delete;
    ChatService& operator=(const ChatService&) = delete;

    std::mutex mutex_;
    std::unordered_map<std::string, int> online_users_;
    std::deque<std::string> message_history_;
};
