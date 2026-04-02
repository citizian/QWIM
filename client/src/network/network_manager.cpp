#include "network_manager.h"
#include "../utils/logger.h"
#include <QJsonDocument>
#include <QtEndian>

NetworkManager::NetworkManager(QObject *parent) : QObject(parent) {
  m_socket = new QTcpSocket(this);
  m_expectedSize = 0;

  connect(m_socket, &QTcpSocket::connected, this, &NetworkManager::connected);
  connect(m_socket, &QTcpSocket::disconnected, this,
          &NetworkManager::disconnected);
  connect(m_socket, &QTcpSocket::readyRead, this, &NetworkManager::onReadyRead);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  connect(m_socket, &QTcpSocket::errorOccurred, this,
          [this](QTcpSocket::SocketError err) {
            emit connectionError(m_socket->errorString());
          });
#endif
}

void NetworkManager::connectToServer(const QString &ip, quint16 port) {
  Logger::logInfo("Connecting to " + ip + ":" + QString::number(port));
  m_expectedSize = 0; // reset on new connection
  m_socket->connectToHost(ip, port);
}

void NetworkManager::disconnectFromServer() { m_socket->disconnectFromHost(); }

void NetworkManager::sendMessage(const NetMessage &msg) {
  if (m_socket->state() == QAbstractSocket::ConnectedState) {
    QByteArray payload = msg.toJson();
    quint32 len = qToBigEndian<quint32>(static_cast<quint32>(payload.size()));

    QByteArray packet;
    packet.append(reinterpret_cast<const char *>(&len), 4);
    packet.append(payload);

    m_socket->write(packet);
    m_socket->flush();
  } else {
    Logger::logError("Tried to send message when disconnected.");
  }
}

void NetworkManager::onReadyRead() {
  while (true) {
    if (m_expectedSize == 0) {
      // Read 4 byte header
      if (m_socket->bytesAvailable() >= 4) {
        QByteArray lenBytes = m_socket->read(4);
        m_expectedSize = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(lenBytes.constData()));
      } else {
        break; // Need more data for header
      }
    }

    if (m_expectedSize > 0) {
      // Read payload
      if (m_socket->bytesAvailable() >= m_expectedSize) {
        QByteArray payload = m_socket->read(m_expectedSize);
        m_expectedSize = 0; // Reset for the next packet

        NetMessage msg = NetMessage::fromJson(payload);
        if (msg.type != "unknown") {
          emit messageReceived(msg);
        } else {
          Logger::logError("Received unknown message format or partial data.");
        }
      } else {
        break; // Need more data for payload
      }
    }
  }
}
