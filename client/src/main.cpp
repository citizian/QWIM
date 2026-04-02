#include "gui/mainwindow.h"
#include "core/app_core.h"
#include <QApplication>

int main(int argc, char *argv[]) {
  QApplication a(argc, argv);
  
  AppCore core;
  MainWindow w;
  
  // Wire View signals to Core slots (User Intents -> Controller)
  QObject::connect(&w, &MainWindow::connectRequested, &core, &AppCore::requestConnect);
  QObject::connect(&w, &MainWindow::loginRequested, &core, &AppCore::requestLogin);
  QObject::connect(&w, &MainWindow::chatSendRequested, &core, &AppCore::requestSendChat);
  
  // Wire Core signals to View slots (Controller Updates -> View)
  QObject::connect(&core, &AppCore::renderMessage, &w, &MainWindow::displayMessage);
  QObject::connect(&core, &AppCore::connectionStateChanged, &w, &MainWindow::updateConnectionState);
  QObject::connect(&core, &AppCore::loginStateChanged, &w, &MainWindow::updateLoginState);
  
  w.show();
  return a.exec();
}
