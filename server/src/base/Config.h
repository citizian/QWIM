#pragma once

#include <string>
#include <unordered_map>

class Config {
public:
    static Config& instance();

    bool load(const std::string& file);

    int getInt(const std::string& key, int default_value = 0);
    std::string getString(const std::string& key, const std::string& default_value = "");

private:
    Config() = default;
    ~Config() = default;
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::unordered_map<std::string, std::string> data;
};
