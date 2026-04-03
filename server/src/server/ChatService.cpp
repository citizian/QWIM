#include "ChatService.h"
#include "Config.h"
#include "IMServer.h"
#include "Logger.h"
#include "Router.h"
#include "../db/UserModel.h"
#include "../db/MessageModel.h"
#include "../base/JwtUtils.h"
#include <netinet/in.h>

ChatService &ChatService::instance() {
  static ChatService service;
  return service;
}

void ChatService::init() {
  Router::instance().registerHandler(
      "register",
      std::bind(&ChatService::handleRegister, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));
  Router::instance().registerHandler(
      "login", std::bind(&ChatService::handleLogin, this, std::placeholders::_1,
                         std::placeholders::_2, std::placeholders::_3));
  Router::instance().registerHandler(
      "chat", std::bind(&ChatService::handleChat, this, std::placeholders::_1,
                        std::placeholders::_2, std::placeholders::_3));
  Router::instance().registerHandler(
      "private",
      std::bind(&ChatService::handlePrivate, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));
  Router::instance().registerHandler(
      "list", std::bind(&ChatService::handleList, this, std::placeholders::_1,
                        std::placeholders::_2, std::placeholders::_3));
  Router::instance().registerHandler(
      "heartbeat",
      std::bind(&ChatService::handleHeartbeat, this, std::placeholders::_1,
                std::placeholders::_2, std::placeholders::_3));
}

void ChatService::onDisconnect(std::shared_ptr<Connection> conn) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!conn->username.empty()) {
    std::string user = conn->username;
    online_users_.erase(user);

    nlohmann::json sys_j;
    sys_j["type"] = "system";
    sys_j["msg"] = user + " left";
    std::string sys_str = sys_j.dump();

    // Broadcast out of lock if possible, but IMServer's broadcast handles its
    // own lock. We defer to IMServer to broadcast it. We don't have server ptr
    // here unless we pass it or store it. Since we need server ptr, maybe
    // IMServer handles broadcast on its own during disconnect, but now the
    // state is in ChatService. Let's rely on IMServer calling broadcast
    // directly during its removeClient instead, OR we provide a callback. For
    // simplicity, we'll let IMServer do the broadcasting for disconnects, or we
    // pass IMServer* to onDisconnect.
  }
}

void ChatService::handleRegister(std::shared_ptr<Connection> conn,
                                 const nlohmann::json &payload,
                                 IMServer *server) {
  std::string user = payload.value("user", "");
  std::string password = payload.value("password", "");

  nlohmann::json res_j;
  res_j["type"] = "system";

  if (user.empty() || password.empty()) {
    res_j["msg"] = "register failed: empty user or password";
  } else if (UserModel::registerUser(user, password)) {
    res_j["msg"] = "register success";
  } else {
    res_j["msg"] = "register failed: username might exist";
  }

  std::string res_str = res_j.dump();
  uint32_t n_len = htonl(res_str.length());
  std::vector<char> packet(4 + res_str.length());
  memcpy(packet.data(), &n_len, 4);
  memcpy(packet.data() + 4, res_str.c_str(), res_str.length());
  conn->write_data(packet.data(), packet.size());
}

void ChatService::handleLogin(std::shared_ptr<Connection> conn, const nlohmann::json& payload, IMServer* server) {
    std::string user = payload.value("user", "");
    std::string password = payload.value("password", "");
    std::string token = payload.value("token", "");

    bool auth_success = false;

    if (!token.empty()) {
        std::string token_user;
        if (JwtUtils::verifyToken(token, token_user)) {
            user = token_user;
            auth_success = true;
        }
    }

    if (!auth_success) {
        if (!UserModel::verifyUser(user, password)) {
            nlohmann::json err_j;
            err_j["type"] = "system";
            err_j["msg"] = "login failed: invalid username, password, or token";
            std::string err_str = err_j.dump();
            uint32_t n_len = htonl(err_str.length());
            std::vector<char> packet(4 + err_str.length());
            memcpy(packet.data(), &n_len, 4);
            memcpy(packet.data() + 4, err_str.c_str(), err_str.length());
            conn->write_data(packet.data(), packet.size());
            
            LOG_INFO << "Failed login attempt for user '" + user + "'";
            return;
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        conn->username = user;
        online_users_[user] = conn->fd;
    }

    LOG_INFO << "User '" + user + "' logged in on fd " + std::to_string(conn->fd);

    // Send login success response with JWT Token
    nlohmann::json success_j;
    success_j["type"] = "login_success";
    success_j["token"] = JwtUtils::generateToken(user);
    std::string suc_str = success_j.dump();
    uint32_t slen = htonl(suc_str.length());
    std::vector<char> pack(4 + suc_str.length());
    memcpy(pack.data(), &slen, 4);
    memcpy(pack.data() + 4, suc_str.c_str(), suc_str.length());
    conn->write_data(pack.data(), pack.size());

    nlohmann::json sys_j;
    sys_j["type"] = "system";
    sys_j["msg"] = user + " joined";
  std::string sys_str = sys_j.dump();
  server->broadcastMessage(conn->fd, sys_str.c_str(), sys_str.length());

  {
    std::vector<std::string> history = MessageModel::getRecentBroadcastMessages(50);
    for (const std::string &hist_msg : history) {
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

void ChatService::handleChat(std::shared_ptr<Connection> conn,
                             const nlohmann::json &payload, IMServer *server) {
  std::string msg = payload.value("msg", "");
  std::string sender = conn->username.empty() ? "Unknown" : conn->username;

  LOG_INFO << "[" + sender + " to all]: " + msg;

  nlohmann::json chat_j;
  chat_j["type"] = "chat";
  chat_j["user"] = sender;
  chat_j["msg"] = msg;
  std::string chat_str = chat_j.dump();

  MessageModel::saveMessage(sender, "ALL", "chat", msg);

  server->broadcastMessage(conn->fd, chat_str.c_str(), chat_str.length());
}

void ChatService::handlePrivate(std::shared_ptr<Connection> conn,
                                const nlohmann::json &payload,
                                IMServer *server) {
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

  MessageModel::saveMessage(sender, to_user, "private", msg);

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

void ChatService::handleList(std::shared_ptr<Connection> conn,
                             const nlohmann::json &payload, IMServer *server) {
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

void ChatService::handleHeartbeat(std::shared_ptr<Connection> conn,
                                  const nlohmann::json &payload,
                                  IMServer *server) {
  // Heartbeat logic is mostly handled at the connection level for timeout
  // renewal wrapper in IMServer, but the actual application layer 'heartbeat'
  // msg just arrives here. There is nothing extra to do in ChatService.
  LOG_INFO << "Received heartbeat from fd " + std::to_string(conn->fd);
}
