#include "net_message.h"
#include "../../third_party/json/json.hpp"
#include <string>

using json = nlohmann::json;

NetMessage::NetMessage()
{
}

QByteArray NetMessage::toJson() const {
    json j;
    j["type"] = type.toStdString();
    
    if (msg.has_value()) {
        j["msg"] = msg.value().toStdString();
    }
    if (user.has_value()) {
        j["user"] = user.value().toStdString();
    }
    if (to.has_value()) {
        j["to"] = to.value().toStdString();
    }
    
    std::string dumped = j.dump();
    return QByteArray(dumped.c_str(), dumped.length());
}

NetMessage NetMessage::fromJson(const QByteArray &data) {
    NetMessage nm;
    try {
        json j = json::parse(data.toStdString());
        
        if (j.contains("type") && j["type"].is_string()) {
            nm.type = QString::fromStdString(j["type"].get<std::string>());
        }
        if (j.contains("msg") && j["msg"].is_string()) {
            nm.msg = QString::fromStdString(j["msg"].get<std::string>());
        }
        if (j.contains("user") && j["user"].is_string()) {
            nm.user = QString::fromStdString(j["user"].get<std::string>());
        }
        if (j.contains("to") && j["to"].is_string()) {
            nm.to = QString::fromStdString(j["to"].get<std::string>());
        }
    } catch (...) {
        nm.type = "unknown";
    }
    return nm;
}
