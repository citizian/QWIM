#ifndef NET_MESSAGE_H
#define NET_MESSAGE_H

#include <QString>
#include <QByteArray>
#include <optional>

class NetMessage
{
public:
    NetMessage();

    QString type = "unknown";
    std::optional<QString> msg;
    std::optional<QString> user;
    std::optional<QString> to;

    QByteArray toJson() const;
    static NetMessage fromJson(const QByteArray &data);
};

#endif // NET_MESSAGE_H
