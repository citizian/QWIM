#pragma once

#include "Connection.h"
#include "json.hpp"
#include <functional>
#include <string>
#include <unordered_map>

class IMServer;

using HandlerFunc = std::function<void(std::shared_ptr<Connection> conn, const nlohmann::json& payload, IMServer* server)>;

class Router {
public:
    static Router& instance();

    void registerHandler(const std::string& type, HandlerFunc handler);
    void route(const std::string& type, std::shared_ptr<Connection> conn, const nlohmann::json& payload, IMServer* server);

private:
    Router() = default;
    ~Router() = default;
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    std::unordered_map<std::string, HandlerFunc> handlers_;
};
