#pragma once
#include <string>

class UserModel {
public:
    // User registration
    static bool registerUser(const std::string& username, const std::string& password);
    
    // User login/verification
    static bool verifyUser(const std::string& username, const std::string& password);
};
