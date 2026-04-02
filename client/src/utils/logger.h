#ifndef LOGGER_H
#define LOGGER_H

#include <QString>

class Logger
{
public:
    static void logInfo(const QString &message);
    static void logError(const QString &message);
};

#endif // LOGGER_H
