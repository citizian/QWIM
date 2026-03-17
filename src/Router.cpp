#include "Router.h"
#include "Logger.h"

Router& Router::instance() {
    static Router router;
    return router;
}

void Router::registerHandler(const std::string& type, HandlerFunc handler) {
    handlers_[type] = handler;
}

void Router::route(const std::string& type, Connection* conn, const nlohmann::json& payload, IMServer* server) {
    auto it = handlers_.find(type);
    if (it != handlers_.end()) {
        it->second(conn, payload, server);
    } else {
        LOG_WARN << "No handler registered for message type: " << type;
    }
}
