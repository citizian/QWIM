#include "logger.h"
#include <QDebug>

void Logger::logInfo(const QString &message)
{
    qDebug().noquote() << "[INFO]" << message;
}

void Logger::logError(const QString &message)
{
    qWarning().noquote() << "[ERROR]" << message;
}
