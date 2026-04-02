#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <QString>

class AppConfig
{
public:
    static AppConfig& instance();

    void load();
    void save();

private:
    AppConfig();
    ~AppConfig() = default;
};

#endif // APP_CONFIG_H
