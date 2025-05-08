#include "mainwindow.h"
#include "ui_mainwindow.h" // Подключаем сгенерированный заголовок из .ui файла
#include <QTimer>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Инициализируем массивы указателей на виджеты
    m_statusLabels << ui->valueStatus_0 << ui->valueStatus_1 << ui->valueStatus_2 << ui->valueStatus_3;
    m_lakLabels << ui->valueLAK_0 << ui->valueLAK_1 << ui->valueLAK_2 << ui->valueLAK_3;
    m_lastSentLabels << ui->valueLastSent_0 << ui->valueLastSent_1 << ui->valueLastSent_2 << ui->valueLastSent_3;
    m_lastRecvLabels << ui->valueLastRecv_0 << ui->valueLastRecv_1 << ui->valueLastRecv_2 << ui->valueLastRecv_3;

    // Инициализируем начальное состояние
    for(int i = 0; i < 4; ++i) {
         m_statusLabels[i]->setText(statusToString(0)); // UVM_LINK_INACTIVE = 0
         m_statusLabels[i]->setStyleSheet(statusToStyleSheet(0));
         m_lakLabels[i]->setText("0x??");
         m_lastSentLabels[i]->setText("Type: - Num: -");
         m_lastRecvLabels[i]->setText("Type: - Num: -");
    }


    m_client = new UvmMonitorClient(this);

    // Соединяем сигнал от клиента со слотом обновления GUI
    connect(m_client, &UvmMonitorClient::svmStatusUpdated, this, &MainWindow::updateSvmDisplay);
    connect(m_client, &UvmMonitorClient::connectionStatusChanged, this, &MainWindow::updateConnectionStatus);

    // Запускаем подключение (можно сделать по кнопке)
    m_client->connectToServer(); // Подключаемся сразу
    // Или сразу: m_client->connectToServer();

    ui->statusbar->showMessage("Connecting to UVM App...");

}

MainWindow::~MainWindow()
{
    delete ui;
    // m_client удалится автоматически, т.к. this является его parent
}

// Слот обновления данных для одного SVM
void MainWindow::updateSvmDisplay(const SvmStatusData &data)
{
    int id = data.id;
    if (id < 0 || id >= m_statusLabels.size()) {
        qWarning() << "Received status for invalid ID:" << id;
        return;
    }

    // Обновляем виджеты
    m_statusLabels[id]->setText(statusToString(data.status));
    m_statusLabels[id]->setStyleSheet(statusToStyleSheet(data.status));
    m_lakLabels[id]->setText(QString("0x%1").arg(data.lak, 2, 16, QChar('0').toUpper()));
    m_lastSentLabels[id]->setText(QString("Type: %1 Num: %2").arg(data.lastSentType).arg(data.lastSentNum));
    m_lastRecvLabels[id]->setText(QString("Type: %1 Num: %2").arg(data.lastRecvType).arg(data.lastRecvNum));
    // Добавить обновление других полей...
}

// Слот обновления статуса подключения к UVM
void MainWindow::updateConnectionStatus(bool connected, const QString &message)
{
     ui->statusbar->showMessage(message);
     if (!connected) {
         // Сбросить отображение всех SVM в INACTIVE при разрыве связи с UVM
         for(int i = 0; i < 4; ++i) {
            SvmStatusData resetData;
            resetData.id = i;
            resetData.status = 0; // INACTIVE
            resetData.lak = 0;
            resetData.lastSentType = -1;
            resetData.lastSentNum = -1;
            resetData.lastRecvType = -1;
            resetData.lastRecvNum = -1;
            updateSvmDisplay(resetData);
         }
     }
}


// Вспомогательная функция для преобразования статуса в строку
QString MainWindow::statusToString(int status) {
    switch (status) {
        case 0: return "INACTIVE";   // UVM_LINK_INACTIVE
        case 1: return "CONNECTING"; // UVM_LINK_CONNECTING
        case 2: return "ACTIVE";     // UVM_LINK_ACTIVE
        case 3: return "FAILED";     // UVM_LINK_FAILED
        case 4: return "DISCONNECTING"; // UVM_LINK_DISCONNECTING
        default: return "UNKNOWN";
    }
}

// Вспомогательная функция для получения стиля по статусу
QString MainWindow::statusToStyleSheet(int status) {
     QString style = "padding: 2px; color: black; font-weight: bold;";
     switch (status) {
         case 0: style += "background-color: lightgray;"; break; // INACTIVE
         case 1: style += "background-color: yellow;"; break;    // CONNECTING
         case 2: style += "background-color: lightgreen;"; break; // ACTIVE
         case 3: style += "background-color: lightcoral;"; break;  // FAILED
         case 4: style += "background-color: orange;"; break;   // DISCONNECTING
         default: style += "background-color: white;"; break;   // UNKNOWN
     }
     return style;
}