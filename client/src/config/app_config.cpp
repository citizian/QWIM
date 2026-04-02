#include "app_config.h"

AppConfig& AppConfig::instance()
{
    static AppConfig config;
    return config;
}

AppConfig::AppConfig()
{
}

void AppConfig::load()
{
}

void AppConfig::save()
{
}
