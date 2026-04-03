#include "UserModel.h"
#include "MySQLPool.h"
#include "Logger.h"
#include <cstring>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>

static std::string hashPassword(const std::string& password) {
    std::string salted = password + "_QwIm_s@lt_591"; // 加盐脱敏
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, salted.c_str(), salted.size());
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

bool UserModel::registerUser(const std::string& username, const std::string& password) {
    MySQLConnectionGuard guard(&MySQLPool::instance());
    MYSQL* conn = guard.get();
    if (!conn) return false;

    // SQL 注入防范：全量转义用户输入
    char* escaped_user = new char[username.length() * 2 + 1];
    mysql_real_escape_string(conn, escaped_user, username.c_str(), username.length());
    
    // 密码加密
    std::string hashed_pw = hashPassword(password);

    std::string sql = "INSERT INTO users (username, password) VALUES ('" + 
                      std::string(escaped_user) + "', '" + hashed_pw + "')";
                      
    delete[] escaped_user;

    if (mysql_query(conn, sql.c_str()) != 0) {
        LOG_ERROR << "Register insert failed: " << mysql_error(conn);
        return false;
    }
    return true;
}

bool UserModel::verifyUser(const std::string& username, const std::string& password) {
    MySQLConnectionGuard guard(&MySQLPool::instance());
    MYSQL* conn = guard.get();
    if (!conn) return false;

    char* escaped_user = new char[username.length() * 2 + 1];
    mysql_real_escape_string(conn, escaped_user, username.c_str(), username.length());
    
    std::string hashed_pw = hashPassword(password);

    std::string sql = "SELECT id FROM users WHERE username='" + std::string(escaped_user) + 
                      "' AND password='" + hashed_pw + "'";
                      
    delete[] escaped_user;

    if (mysql_query(conn, sql.c_str()) != 0) {
        LOG_ERROR << "Verify query failed: " << mysql_error(conn);
        return false;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) return false;

    bool exists = (mysql_num_rows(res) > 0);
    mysql_free_result(res);
    
    return exists;
}
