#include "JwtUtils.h"
#include <openssl/hmac.h>
#include <vector>
#include "json.hpp"
#include <ctime>
#include <cctype>

static const std::string SECRET_KEY = "QwIM_SuPeR_sEcrEt_kEy_2026!";

std::string JwtUtils::base64UrlEncode(const std::string& data) {
    static const char cb64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string ret;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    const char* bytes = data.c_str();
    size_t in_len = data.length();

    while (in_len--) {
        char_array_3[i++] = *(bytes++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for (i = 0; (i < 4); i++) ret += cb64[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (int j = 0; (j < i + 1); j++) ret += cb64[char_array_4[j]];
    }
    return ret;
}

std::string JwtUtils::base64UrlDecode(const std::string& encoded) {
    auto is_base64url = [](unsigned char c) {
        return (std::isalnum(c) || (c == '-') || (c == '_'));
    };
    int in_len = encoded.size();
    int i = 0, in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;

    while (in_len-- && (encoded[in_] != '=') && is_base64url(encoded[in_])) {
        char_array_4[i++] = encoded[in_]; in_++;
        if (i == 4) {
            for (i = 0; i < 4; i++) {
                if (char_array_4[i] >= 'A' && char_array_4[i] <= 'Z') char_array_4[i] -= 'A';
                else if (char_array_4[i] >= 'a' && char_array_4[i] <= 'z') char_array_4[i] = char_array_4[i] - 'a' + 26;
                else if (char_array_4[i] >= '0' && char_array_4[i] <= '9') char_array_4[i] = char_array_4[i] - '0' + 52;
                else if (char_array_4[i] == '-') char_array_4[i] = 62;
                else if (char_array_4[i] == '_') char_array_4[i] = 63;
            }
            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
            for (i = 0; (i < 3); i++) ret += char_array_3[i];
            i = 0;
        }
    }
    if (i) {
        for (int j = i; j < 4; j++) char_array_4[j] = 0;
        for (int j = 0; j < 4; j++) {
            if (char_array_4[j] >= 'A' && char_array_4[j] <= 'Z') char_array_4[j] -= 'A';
            else if (char_array_4[j] >= 'a' && char_array_4[j] <= 'z') char_array_4[j] = char_array_4[j] - 'a' + 26;
            else if (char_array_4[j] >= '0' && char_array_4[j] <= '9') char_array_4[j] = char_array_4[j] - '0' + 52;
            else if (char_array_4[j] == '-') char_array_4[j] = 62;
            else if (char_array_4[j] == '_') char_array_4[j] = 63;
        }
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        for (int j = 0; (j < i - 1); j++) ret += char_array_3[j];
    }
    return ret;
}

std::string JwtUtils::hmacSha256(const std::string& data, const std::string& key) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen;
    HMAC(EVP_sha256(), key.c_str(), key.length(),
         (const unsigned char*)data.c_str(), data.length(), hash, &hashLen);
    return std::string((char*)hash, hashLen);
}

std::string JwtUtils::generateToken(const std::string& username) {
    nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}};
    
    long int exp = std::time(nullptr) + 7 * 24 * 3600; // 7 days expiry
    nlohmann::json payload = {{"user", username}, {"exp", exp}};
    
    std::string encodedHeader = base64UrlEncode(header.dump());
    std::string encodedPayload = base64UrlEncode(payload.dump());
    std::string signaturePayload = encodedHeader + "." + encodedPayload;
    
    std::string signature = base64UrlEncode(hmacSha256(signaturePayload, SECRET_KEY));
    return signaturePayload + "." + signature;
}

bool JwtUtils::verifyToken(const std::string& token, std::string& out_username) {
    size_t firstDot = token.find('.');
    size_t lastDot = token.rfind('.');
    
    if (firstDot == std::string::npos || lastDot == std::string::npos || firstDot == lastDot) {
        return false;
    }
    
    std::string encodedHeader = token.substr(0, firstDot);
    std::string encodedPayload = token.substr(firstDot + 1, lastDot - firstDot - 1);
    std::string signature = token.substr(lastDot + 1);
    
    std::string expectedSignature = base64UrlEncode(hmacSha256(encodedHeader + "." + encodedPayload, SECRET_KEY));
    if (signature != expectedSignature) return false;
    
    std::string payloadStr = base64UrlDecode(encodedPayload);
    try {
        nlohmann::json payload = nlohmann::json::parse(payloadStr);
        if (payload.contains("exp")) {
            long int exp = payload["exp"];
            if (std::time(nullptr) > exp) {
                return false;
            }
        }
        if (payload.contains("user")) {
            out_username = payload["user"];
            return true;
        }
    } catch (...) {
        return false;
    }
    return false;
}
