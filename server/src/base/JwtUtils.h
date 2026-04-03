#pragma once
#include <string>

class JwtUtils {
public:
    static std::string generateToken(const std::string& username);
    static bool verifyToken(const std::string& token, std::string& out_username);

private:
    static std::string base64UrlEncode(const std::string& data);
    static std::string base64UrlDecode(const std::string& data);
    static std::string hmacSha256(const std::string& data, const std::string& key);
};
