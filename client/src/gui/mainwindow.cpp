#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);

  statelogin = new QLabel("Unlogged", this);
  stateconnect = new QLabel("Disconnected", this);
  ui->statusbar->addPermanentWidget(stateconnect);
  ui->statusbar->addPermanentWidget(statelogin);

  ui->lineip->setText("127.0.0.1");
  ui->lineport->setText("8081");
  ui->lineusername->setText("yza");

  connect(ui->btnconnect, &QPushButton::clicked, this, &MainWindow::onConnectBtnClicked);
  connect(ui->btnlogin, &QPushButton::clicked, this, &MainWindow::onLoginBtnClicked);
  connect(ui->btnsend, &QPushButton::clicked, this, &MainWindow::onSendBtnClicked);

  ui->texthistory->setReadOnly(true);
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::onConnectBtnClicked() {
    emit connectRequested(ui->lineip->text(), ui->lineport->text().toUShort());
}

void MainWindow::onLoginBtnClicked() {
    QString user = ui->lineusername->text();
    if(!user.isEmpty()) {
        emit loginRequested(user);
    }
}

void MainWindow::onSendBtnClicked() {
    QString text = ui->textchat->toPlainText();
    if(!text.isEmpty()) {
        emit chatSendRequested(text);
        ui->textchat->clear();
    }
}

void MainWindow::displayMessage(const QString &msg) {
    ui->texthistory->append(msg);
}

void MainWindow::updateConnectionState(bool connected, const QString &stateString) {
    Q_UNUSED(connected);
    stateconnect->setText(stateString);
}

void MainWindow::updateLoginState(const QString &loginString) {
    statelogin->setText(loginString);
}
