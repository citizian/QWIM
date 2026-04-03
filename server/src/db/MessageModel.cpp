#include "MessageModel.h"
#include "MySQLPool.h"
#include "Logger.h"

bool MessageModel::saveMessage(const std::string& sender, const std::string& receiver, 
                               const std::string& msgType, const std::string& content) {
    MySQLConnectionGuard guard(&MySQLPool::instance());
    MYSQL* conn = guard.get();
    if (!conn) return false;

    char* escaped_sender = new char[sender.length() * 2 + 1];
    char* escaped_receiver = new char[receiver.length() * 2 + 1];
    char* escaped_type = new char[msgType.length() * 2 + 1];
    char* escaped_content = new char[content.length() * 2 + 1];

    mysql_real_escape_string(conn, escaped_sender, sender.c_str(), sender.length());
    mysql_real_escape_string(conn, escaped_receiver, receiver.c_str(), receiver.length());
    mysql_real_escape_string(conn, escaped_type, msgType.c_str(), msgType.length());
    mysql_real_escape_string(conn, escaped_content, content.c_str(), content.length());

    std::string sql = "INSERT INTO messages (sender, receiver, msg_type, content) VALUES ('" + 
                      std::string(escaped_sender) + "', '" + std::string(escaped_receiver) + "', '" + 
                      std::string(escaped_type) + "', '" + std::string(escaped_content) + "')";

    delete[] escaped_sender;
    delete[] escaped_receiver;
    delete[] escaped_type;
    delete[] escaped_content;

    if (mysql_query(conn, sql.c_str()) != 0) {
        LOG_ERROR << "Failed to save message: " << mysql_error(conn);
        return false;
    }
    return true;
}

std::vector<std::string> MessageModel::getRecentBroadcastMessages(int limit) {
    std::vector<std::string> history;
    MySQLConnectionGuard guard(&MySQLPool::instance());
    MYSQL* conn = guard.get();
    if (!conn) return history;

    std::string sql = "SELECT sender, content FROM messages WHERE msg_type='chat' ORDER BY id DESC LIMIT " + std::to_string(limit);
    if (mysql_query(conn, sql.c_str()) != 0) {
        LOG_ERROR << "Failed to fetch messages: " << mysql_error(conn);
        return history;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return history;

    MYSQL_ROW row;
    // The results come back descending, we need to reverse them to chronological order
    std::vector<std::string> raw_history;
    while ((row = mysql_fetch_row(res))) {
        std::string sender = row[0] ? row[0] : "Unknown";
        std::string content = row[1] ? row[1] : "";
        raw_history.push_back(sender + ": " + content);
    }
    mysql_free_result(res);

    for (auto it = raw_history.rbegin(); it != raw_history.rend(); ++it) {
        history.push_back(*it);
    }
    
    return history;
}
