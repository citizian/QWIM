#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QLabel>
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

signals:
  void connectRequested(const QString &ip, quint16 port);
  void loginRequested(const QString &username);
  void chatSendRequested(const QString &content);

public slots:
  void displayMessage(const QString &msg);
  void updateConnectionState(bool connected, const QString &stateString);
  void updateLoginState(const QString &loginString);

private slots:
  void onConnectBtnClicked();
  void onLoginBtnClicked();
  void onSendBtnClicked();

private:
  Ui::MainWindow *ui;
  QLabel *stateconnect;
  QLabel *statelogin;
};
#endif // MAINWINDOW_H
