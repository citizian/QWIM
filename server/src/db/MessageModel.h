#pragma once
#include <string>
#include <vector>

class MessageModel {
public:
    // Save a message to DB
    static bool saveMessage(const std::string& sender, const std::string& receiver, 
                            const std::string& msgType, const std::string& content);
                            
    // Fetch recent broadcast (hall) messages
    static std::vector<std::string> getRecentBroadcastMessages(int limit);
};
