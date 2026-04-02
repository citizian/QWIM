#include "app_core.h"
#include <QTime>
#include <QTimer>

AppCore::AppCore(QObject *parent) : QObject(parent) {
  m_netManager = new NetworkManager(this);
  m_heartbeatTimer = new QTimer(this);

  // Set heartbeat interval to 10 seconds
  m_heartbeatTimer->setInterval(25000);

  connect(m_netManager, &NetworkManager::connected, this,
          &AppCore::onNetworkConnected);
  connect(m_netManager, &NetworkManager::disconnected, this,
          &AppCore::onNetworkDisconnected);
  connect(m_netManager, &NetworkManager::connectionError, this,
          &AppCore::onConnectionError);
  connect(m_netManager, &NetworkManager::messageReceived, this,
          &AppCore::onMessageReceived);

  connect(m_heartbeatTimer, &QTimer::timeout, this,
          &AppCore::onHeartbeatTimeout);
}

AppCore::~AppCore() {}

NetworkManager *AppCore::network() const { return m_netManager; }

void AppCore::requestConnect(const QString &ip, quint16 port) {
  emit connectionStateChanged(false, "Connecting...");
  m_netManager->connectToServer(ip, port);
}

void AppCore::requestLogin(const QString &username) {
  m_currentUser = username;
  NetMessage msg;
  msg.type = "login";
  msg.user = username;
  m_netManager->sendMessage(msg);

  emit loginStateChanged("Logged in as " + username);
}

void AppCore::requestSendChat(const QString &content) {
  if (m_currentUser.isEmpty())
    return;

  NetMessage msg;
  msg.type = "chat";
  msg.user = m_currentUser;
  msg.msg = content;
  m_netManager->sendMessage(msg);
}

void AppCore::onNetworkConnected() {
  m_heartbeatTimer->start();
  emit connectionStateChanged(true, "Connected");
  emit renderMessage("[System] Connected to server.");
}

void AppCore::onNetworkDisconnected() {
  m_heartbeatTimer->stop();
  emit connectionStateChanged(false, "Disconnected");
  emit loginStateChanged("Unlogged");
  emit renderMessage("[System] Disconnected from server.");
}

void AppCore::onConnectionError(const QString &err) {
  m_heartbeatTimer->stop();
  emit connectionStateChanged(false, "Error");
  emit renderMessage("[Error] " + err);
}

void AppCore::onHeartbeatTimeout() {
  NetMessage msg;
  msg.type = "heartbeat";
  m_netManager->sendMessage(msg);
}

void AppCore::onMessageReceived(const NetMessage &msg) {
  QString timeStr = QTime::currentTime().toString("HH:mm:ss");

  // Base formatting: [Time] [Type]
  QString formattedMsg = QString("[%1] [%2]").arg(timeStr, msg.type);

  // Add User if exists: [Time] [Type] User
  if (msg.user.has_value()) {
    formattedMsg += " " + msg.user.value();
  }

  // Add To if exists: [Time] [Type] User (to: Target)
  if (msg.to.has_value()) {
    formattedMsg += QString(" (to: %1)").arg(msg.to.value());
  }

  // Add Content if exists: [Time] [Type] User: Content
  if (msg.msg.has_value()) {
    if (msg.user.has_value() || msg.to.has_value()) {
      formattedMsg += ": ";
    } else {
      formattedMsg += " - ";
    }
    formattedMsg += msg.msg.value();
  }

  emit renderMessage(formattedMsg);
}
