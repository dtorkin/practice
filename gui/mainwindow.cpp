#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTimer>
#include <QDebug>
#include <QListWidget> // Включаем для QListWidget

// --- Вспомогательная функция для имен типов сообщений (упрощенная) ---
// В реальном приложении ее можно сделать более полной или брать из общего источника
QString MainWindow::messageTypeToName(int type) {
    // Это очень упрощенный пример, нужно будет расширить или использовать
    // определения из protocol_defs.h, если они доступны в C++
    switch (type) {
        case 128: return "InitChan";
        case 129: return "ConfInit";
        case 1:   return "ProvKontr";
        case 3:   return "PodtvKontr";
        case 2:   return "VydRezKontr";
        case 4:   return "RezKontr";
        case 6:   return "VydSostLin";
        case 7:   return "SostLin";
        case 160: return "ParamSO";
        case 200: return "Param3TSO";
        case 161: return "TimeRef";
        case 162: return "Reper";
        case 170: return "ParamSDR";
        case 210: return "ParamTSD";
        case 255: return "NavData";
        case 81:  return "Pomeha";
        case 254: return "Predupr";
        default:  return QString("T:%1").arg(type);
    }
}
// --- Конец вспомогательной функции ---


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // Инициализируем массивы указателей на виджеты
    m_statusLabels << ui->valueStatus_0 << ui->valueStatus_1 << ui->valueStatus_2 << ui->valueStatus_3;
    m_lakLabels    << ui->valueLAK_0    << ui->valueLAK_1    << ui->valueLAK_2    << ui->valueLAK_3;
    m_bcbLabels    << ui->valueBCB_0    << ui->valueBCB_1    << ui->valueBCB_2    << ui->valueBCB_3;
    m_historyWidgets << ui->historyList_0 << ui->historyList_1 << ui->historyList_2 << ui->historyList_3;
    m_errorLabels  << ui->errorStatusLabel_0 << ui->errorStatusLabel_1 << ui->errorStatusLabel_2 << ui->errorStatusLabel_3;


    // Инициализируем начальное состояние
    for(int i = 0; i < m_statusLabels.size(); ++i) { // Используем m_statusLabels.size()
         if (m_statusLabels[i]) m_statusLabels[i]->setText(statusToString(0)); // UVM_LINK_INACTIVE = 0
         if (m_statusLabels[i]) m_statusLabels[i]->setStyleSheet(statusToStyleSheet(0));
         if (m_lakLabels[i]) m_lakLabels[i]->setText("0x??");
         if (m_bcbLabels[i]) m_bcbLabels[i]->setText("0");
         if (m_errorLabels[i]) m_errorLabels[i]->setText("Status: OK");
         if (m_historyWidgets[i]) m_historyWidgets[i]->clear(); // Очищаем историю

         // Инициализируем предыдущие номера сообщений
         m_prevSentNum[i] = -1; // Используем -1 как признак "еще не было"
         m_prevRecvNum[i] = -1;
    }


    m_client = new UvmMonitorClient(this);
    connect(m_client, &UvmMonitorClient::svmStatusUpdated, this, &MainWindow::updateSvmDisplay);
    connect(m_client, &UvmMonitorClient::connectionStatusChanged, this, &MainWindow::updateConnectionStatus);

    m_client->connectToServer();
    ui->statusbar->showMessage("Attempting to connect to UVM App...");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::updateSvmDisplay(const SvmStatusData &data)
{
    int id = data.id;
    if (id < 0 || id >= m_statusLabels.size() || !m_statusLabels[id]) { // Проверяем валидность виджетов
        qWarning() << "Received status for invalid ID or UI not ready:" << id;
        return;
    }

    // Обновляем статус, LAK, BCB
    m_statusLabels[id]->setText(statusToString(data.status));
    m_statusLabels[id]->setStyleSheet(statusToStyleSheet(data.status));
    m_lakLabels[id]->setText(QString("0x%1").arg(data.lak, 2, 16, QChar('0')).toUpper());
    m_bcbLabels[id]->setText(QString::number(data.bcb));

    // Обновляем историю сообщений
    if (m_historyWidgets[id]) {
        // Отправленные сообщения
        if (data.lastSentNum >= 0 && data.lastSentNum != m_prevSentNum.value(id, -1)) {
            QString sentMsg = QString("S > %1 (N:%2)")
                                  .arg(messageTypeToName(data.lastSentType))
                                  .arg(data.lastSentNum);
            m_historyWidgets[id]->addItem(sentMsg);
            m_prevSentNum[id] = data.lastSentNum;
        }
        // Полученные сообщения
        if (data.lastRecvNum >= 0 && data.lastRecvNum != m_prevRecvNum.value(id, -1)) {
            QString recvMsg = QString("R < %1 (N:%2)")
                                  .arg(messageTypeToName(data.lastRecvType))
                                  .arg(data.lastRecvNum);
            m_historyWidgets[id]->addItem(recvMsg);
            m_prevRecvNum[id] = data.lastRecvNum;
        }

        // Ограничиваем размер истории
        while (m_historyWidgets[id]->count() > MAX_HISTORY_ITEMS) {
            delete m_historyWidgets[id]->takeItem(0); // Удаляем самый старый элемент
        }
        m_historyWidgets[id]->scrollToBottom(); // Прокручиваем вниз
    }

    // Обновляем статус ошибок
    if (m_errorLabels[id]) {
        QString errorText = "Status: ";
        bool hasError = false;
        QString errorColor = "green";

        if (data.status == 3 /*UVM_LINK_FAILED*/) { // Используем числовое значение enum UvmLinkStatus
            errorText += "<font color='red'>FAILED</font>; ";
            hasError = true;
            errorColor = "lightcoral";
        }
        if (data.timeoutDetected) {
            errorText += "<font color='red'>TIMEOUT</font>; ";
            hasError = true;
            errorColor = "lightcoral";
        }
        if (data.lakFailDetected) {
            errorText += "<font color='red'>LAK Mismatch</font>; ";
            hasError = true;
            errorColor = "lightcoral";
        }
        if (data.ctrlFailDetected) { // data.rsk != 0xFF && data.rsk != 0x3F
            errorText += QString("<font color='orange'>CtrlFail(RSK:0x%1)</font>; ").arg(data.rsk, 2, 16, QChar('0'));
            hasError = true;
            if (errorColor != "lightcoral") errorColor = "orange";
        }
        if (data.warnTKS != 0) {
            errorText += QString("<font color='orange'>Warn(TKS:%1)</font>; ").arg(data.warnTKS);
            hasError = true;
             if (errorColor != "lightcoral") errorColor = "orange";
        }
        if (data.simDisconnect) {
            errorText += QString("<font color='blue'>SimDisc(in %1)</font>; ").arg(data.discCountdown);
            // не считаем ошибкой, просто информация
        }

        if (!hasError && data.status == 2 /*UVM_LINK_ACTIVE*/) {
            errorText += "<font color='green'>OK</font>";
        } else if (!hasError) {
            errorText += "N/A"; // Если не активен и нет ошибок
        }


        m_errorLabels[id]->setText(errorText);
        // Можно менять стиль всего errorLabels[id]
        // m_errorLabels[id]->setStyleSheet(QString("background-color: %1;").arg(errorColor));
    }
}

void MainWindow::updateConnectionStatus(bool connected, const QString &message)
{
     ui->statusbar->showMessage(message);
     if (!connected) {
         for(int i = 0; i < m_statusLabels.size(); ++i) { // Используем m_statusLabels.size()
            if (m_statusLabels[i]) m_statusLabels[i]->setText(statusToString(0)); // INACTIVE
            if (m_statusLabels[i]) m_statusLabels[i]->setStyleSheet(statusToStyleSheet(0));
            if (m_lakLabels[i]) m_lakLabels[i]->setText("0x??");
            if (m_bcbLabels[i]) m_bcbLabels[i]->setText("0");
            if (m_historyWidgets[i]) m_historyWidgets[i]->clear();
            if (m_errorLabels[i]) m_errorLabels[i]->setText("Status: Disconnected");
            m_prevSentNum[i] = -1;
            m_prevRecvNum[i] = -1;
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