#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <QObject>
#include <QTcpSocket>
#include "../models/net_message.h"

class NetworkManager : public QObject
{
    Q_OBJECT
public:
    explicit NetworkManager(QObject *parent = nullptr);

    void connectToServer(const QString &ip, quint16 port);
    void disconnectFromServer();
    void sendMessage(const NetMessage &msg);

signals:
    void connected();
    void disconnected();
    void messageReceived(NetMessage msg);
    void connectionError(QString errorMsg);

private slots:
    void onReadyRead();

private:
    QTcpSocket *m_socket;
    quint32 m_expectedSize = 0;
};

#endif // NETWORK_MANAGER_H
