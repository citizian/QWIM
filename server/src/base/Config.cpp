#include "Config.h"
#include <fstream>
#include <iostream>

Config& Config::instance() {
    static Config config;
    return config;
}

bool Config::load(const std::string& file) {
    if (file.empty()) return false;
    
    std::ifstream ifs(file);
    if (!ifs.is_open()) {
        std::cerr << "Warning: Could not open config file " << file << ", using defaults.\n";
        return false;
    }

    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);

            data[key] = value;
        }
    }
    return true;
}

int Config::getInt(const std::string& key, int default_value) {
    auto it = data.find(key);
    if (it != data.end()) {
        try {
            return std::stoi(it->second);
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

std::string Config::getString(const std::string& key, const std::string& default_value) {
    auto it = data.find(key);
    if (it != data.end()) {
        return it->second;
    }
    return default_value;
}
