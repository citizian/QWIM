#ifndef APP_CORE_H
#define APP_CORE_H

#include "../models/net_message.h"
#include "../network/network_manager.h"
#include <QObject>
#include <QString>

class AppCore : public QObject {
  Q_OBJECT
public:
  explicit AppCore(QObject *parent = nullptr);
  ~AppCore() override;

  NetworkManager *network() const;

signals:
  void renderMessage(const QString &msg);
  void connectionStateChanged(bool connected, const QString &stateString);
  void loginStateChanged(const QString &stateString);

public slots:
  void requestConnect(const QString &ip, quint16 port);
  void requestLogin(const QString &username);
  void requestSendChat(const QString &content);

private slots:
  void onNetworkConnected();
  void onNetworkDisconnected();
  void onConnectionError(const QString &err);
  void onMessageReceived(const NetMessage &msg);
  void onHeartbeatTimeout();

private:
  NetworkManager *m_netManager;
  QString m_currentUser;
  class QTimer *m_heartbeatTimer;
};

#endif // APP_CORE_H
