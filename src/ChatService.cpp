#include "ChatService.h"
#include "IMServer.h"
#include "Logger.h"
#include "Router.h"
#include "Config.h"

ChatService& ChatService::instance() {
    static ChatService service;
    return service;
}

void ChatService::init() {
    Router::instance().registerHandler("login", std::bind(&ChatService::handleLogin, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    Router::instance().registerHandler("chat", std::bind(&ChatService::handleChat, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    Router::instance().registerHandler("private", std::bind(&ChatService::handlePrivate, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    Router::instance().registerHandler("list", std::bind(&ChatService::handleList, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    Router::instance().registerHandler("heartbeat", std::bind(&ChatService::handleHeartbeat, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

void ChatService::onDisconnect(Connection* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!conn->username.empty()) {
        std::string user = conn->username;
        online_users_.erase(user);

        nlohmann::json sys_j;
        sys_j["type"] = "system";
        sys_j["msg"] = user + " left";
        std::string sys_str = sys_j.dump();

        // Broadcast out of lock if possible, but IMServer's broadcast handles its own lock.
        // We defer to IMServer to broadcast it. 
        // We don't have server ptr here unless we pass it or store it. 
        // Since we need server ptr, maybe IMServer handles broadcast on its own during disconnect, 
        // but now the state is in ChatService.
        // Let's rely on IMServer calling broadcast directly during its removeClient instead, OR we provide a callback.
        // For simplicity, we'll let IMServer do the broadcasting for disconnects, 
        // or we pass IMServer* to onDisconnect.
    }
}

void ChatService::handleLogin(Connection* conn, const nlohmann::json& payload, IMServer* server) {
    std::string user = payload.value("user", "Unknown");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        conn->username = user;
        online_users_[user] = conn->fd;
    }

    LOG_INFO << "User '" + user + "' logged in on fd " + std::to_string(conn->fd);

    nlohmann::json sys_j;
    sys_j["type"] = "system";
    sys_j["msg"] = user + " joined";
    std::string sys_str = sys_j.dump();
    server->broadcastMessage(conn->fd, sys_str.c_str(), sys_str.length());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const std::string &hist_msg : message_history_) {
            nlohmann::json hist_j;
            hist_j["type"] = "history";
            hist_j["msg"] = hist_msg;
            std::string hist_str = hist_j.dump();

            uint32_t hist_len = hist_str.length();
            uint32_t nl = htonl(hist_len);
            std::vector<char> packet(4 + hist_len);
            memcpy(packet.data(), &nl, 4);
            memcpy(packet.data() + 4, hist_str.c_str(), hist_len);
            conn->write_data(packet.data(), packet.size());
        }
    }
}

void ChatService::handleChat(Connection* conn, const nlohmann::json& payload, IMServer* server) {
    std::string msg = payload.value("msg", "");
    std::string sender = conn->username.empty() ? "Unknown" : conn->username;

    LOG_INFO << "[" + sender + " to all]: " + msg;

    nlohmann::json chat_j;
    chat_j["type"] = "chat";
    chat_j["user"] = sender;
    chat_j["msg"] = msg;
    std::string chat_str = chat_j.dump();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string history_entry = sender + ": " + msg;
        message_history_.push_back(history_entry);

        size_t history_size = Config::instance().getInt("history_size", 50);

        while (message_history_.size() > history_size) {
            message_history_.pop_front();
        }
    }

    server->broadcastMessage(conn->fd, chat_str.c_str(), chat_str.length());
}

void ChatService::handlePrivate(Connection* conn, const nlohmann::json& payload, IMServer* server) {
    std::string to_user = payload.value("to", "");
    std::string msg = payload.value("msg", "");
    std::string sender = conn->username.empty() ? "Unknown" : conn->username;

    LOG_INFO << "[" + sender + " -> " + to_user + "]: " + msg;

    int target_fd = -1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (online_users_.count(to_user)) {
            target_fd = online_users_[to_user];
        }
    }

    if (server->isConnectionActive(target_fd)) {
        nlohmann::json private_j;
        private_j["type"] = "private";
        private_j["user"] = sender;
        private_j["msg"] = msg;
        std::string private_str = private_j.dump();

        uint32_t n_len = htonl(private_str.length());
        std::vector<char> packet(4 + private_str.length());
        memcpy(packet.data(), &n_len, 4);
        memcpy(packet.data() + 4, private_str.c_str(), private_str.length());

        server->sendToUser(target_fd, packet.data(), packet.size());
    } else {
        nlohmann::json err_j;
        err_j["type"] = "system";
        err_j["msg"] = "user not online";
        std::string err_str = err_j.dump();

        uint32_t n_len = htonl(err_str.length());
        std::vector<char> packet(4 + err_str.length());
        memcpy(packet.data(), &n_len, 4);
        memcpy(packet.data() + 4, err_str.c_str(), err_str.length());

        conn->write_data(packet.data(), packet.size());
        LOG_INFO << "User '" + to_user + "' not found. Error sent to sender.";
    }
}

void ChatService::handleList(Connection* conn, const nlohmann::json& payload, IMServer* server) {
    nlohmann::json list_j;
    list_j["type"] = "list";
    list_j["users"] = nlohmann::json::array();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &pair : online_users_) {
            list_j["users"].push_back(pair.first);
        }
    }

    std::string list_str = list_j.dump();
    uint32_t n_len = htonl(list_str.length());
    std::vector<char> packet(4 + list_str.length());
    memcpy(packet.data(), &n_len, 4);
    memcpy(packet.data() + 4, list_str.c_str(), list_str.length());

    conn->write_data(packet.data(), packet.size());
    LOG_INFO << "Sent active users list to fd " + std::to_string(conn->fd);
}

void ChatService::handleHeartbeat(Connection* conn, const nlohmann::json& payload, IMServer* server) {
    // Heartbeat logic is mostly handled at the connection level for timeout renewal wrapper in IMServer,
    // but the actual application layer 'heartbeat' msg just arrives here. There is nothing extra to do in ChatService.
    LOG_INFO << "Received heartbeat from fd " + std::to_string(conn->fd);
}
