#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTimer>
#include <QDebug>
#include <QTableWidget>
#include <QPushButton> // Для ui->buttonSaveAllLogs
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QHeaderView>

const int MAX_GUI_SVM_INSTANCES = 4; // Соответствует MAX_SVM_CONFIGS

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_lastDisplayedBcb(MAX_GUI_SVM_INSTANCES, 0) // Инициализируем нулями
{
    ui->setupUi(this);

    // Инициализируем векторы виджетов
    m_statusLabels << ui->valueStatus_0 << ui->valueStatus_1 << ui->valueStatus_2 << ui->valueStatus_3;
    m_lakLabels    << ui->valueLAK_0    << ui->valueLAK_1    << ui->valueLAK_2    << ui->valueLAK_3;
    m_bcbLabels    << ui->valueBCB_0    << ui->valueBCB_1    << ui->valueBCB_2    << ui->valueBCB_3;
    m_logTables    << ui->tableLog_0    << ui->tableLog_1    << ui->tableLog_2    << ui->tableLog_3;
    m_errorDisplays << ui->errorDisplay_0 << ui->errorDisplay_1 << ui->errorDisplay_2 << ui->errorDisplay_3;

    for(int i = 0; i < MAX_GUI_SVM_INSTANCES; ++i) {
         if (m_statusLabels[i]) {
             m_statusLabels[i]->setText(statusToString(0)); // UVM_LINK_INACTIVE
             m_statusLabels[i]->setStyleSheet(statusToStyleSheet(0));
         }
         if (m_lakLabels[i]) m_lakLabels[i]->setText("N/A");
         if (m_bcbLabels[i]) m_bcbLabels[i]->setText("N/A");
         if (m_errorDisplays[i]) {
             m_errorDisplays[i]->setText("Status: OK");
             m_errorDisplays[i]->setStyleSheet("color: green;");
         }
         if (m_logTables[i]) {
             initTableWidget(m_logTables[i]);
         }
    }

    connect(ui->buttonSaveAllLogs, &QPushButton::clicked, this, &MainWindow::onSaveLogAllClicked);

    m_client = new UvmMonitorClient(this);
    connect(m_client, &UvmMonitorClient::newMessageOrEvent, this, &MainWindow::onNewMessageOrEvent);
    connect(m_client, &UvmMonitorClient::connectionStatusChanged, this, &MainWindow::updateConnectionStatus);
    connect(m_client, &UvmMonitorClient::svmLinkStatusChanged, this, &MainWindow::updateSvmLinkStatusDisplay);

    m_client->connectToServer();
    ui->statusbar->showMessage("Attempting to connect to UVM App...");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::initTableWidget(QTableWidget* table) {
    if (!table) return;
    table->setColumnCount(7);
    QStringList headers = {"Время", "Напр/Соб.", "LAK", "Тип", "Имя сообщ.", "Номер", "Детали/BCB"};
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(false); // Чтобы последний столбец не растягивался слишком сильно
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setWordWrap(false);
    table->setColumnWidth(0, 100); // Время
    table->setColumnWidth(1, 80);  // Напр/Соб.
    table->setColumnWidth(2, 60);  // LAK
    table->setColumnWidth(3, 50);  // Тип
    table->setColumnWidth(4, 180); // Имя
    table->setColumnWidth(5, 60);  // Номер
    table->setColumnWidth(6, 250); // Детали - пошире
    table->verticalHeader()->setVisible(false); // Скрыть нумерацию строк
}

void MainWindow::onNewMessageOrEvent(int svmId, const QDateTime &timestamp, const QString &directionOrEventType,
                                   int msgType, const QString &msgName, int msgNum,
                                   int assignedLak, const QString &details)
{
    if (svmId < 0 || svmId >= MAX_GUI_SVM_INSTANCES || !m_logTables[svmId]) {
        qWarning() << "Received message/event for invalid SVM ID:" << svmId;
        return;
    }

    QTableWidget *table = m_logTables[svmId];
    int row = table->rowCount();
    table->insertRow(row);

    table->setItem(row, 0, new QTableWidgetItem(timestamp.toString("hh:mm:ss.zzz")));
    table->setItem(row, 1, new QTableWidgetItem(directionOrEventType));
    table->setItem(row, 2, new QTableWidgetItem((assignedLak >=0) ? QString("0x%1").arg(assignedLak, 2, 16, QChar('0')).toUpper() : "N/A"));

    QString detailsToShow = details; // Основные детали

    if (directionOrEventType == "SENT" || directionOrEventType == "RECV") {
        table->setItem(row, 3, new QTableWidgetItem(QString::number(msgType)));
        table->setItem(row, 4, new QTableWidgetItem(msgName));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(msgNum)));

        // Обновляем BCB QLabel, если это сообщение содержит BCB
        // (UvmMonitorClient должен парсить BCB и передавать его, или мы парсим details здесь)
        if (details.startsWith("BCB=")) { // Простой пример парсинга BCB из деталей
            QString bcbValStr = details.mid(4);
            bool ok;
            quint32 bcbVal = bcbValStr.toUInt(&ok);
            if (ok && m_bcbLabels[svmId] && bcbVal != m_lastDisplayedBcb[svmId]) {
                m_bcbLabels[svmId]->setText(QString::number(bcbVal));
                m_lastDisplayedBcb[svmId] = bcbVal;
            }
        }
        // Для SENT/RECV details могут быть пустыми или содержать что-то еще
    } else { // EVENT
        table->setItem(row, 3, new QTableWidgetItem("-"));
        table->setItem(row, 4, new QTableWidgetItem(directionOrEventType)); // Тип события как "имя"
        table->setItem(row, 5, new QTableWidgetItem("-"));
    }
    table->setItem(row, 6, new QTableWidgetItem(detailsToShow));


    // Обновляем LAK QLabel
    if (m_lakLabels[svmId] && assignedLak >=0) {
        m_lakLabels[svmId]->setText(QString("0x%1").arg(assignedLak, 2, 16, QChar('0')).toUpper());
    }

    // Обновляем errorDisplay
    if (m_errorDisplays[svmId]) {
        if (directionOrEventType == "EVENT") {
            m_errorDisplays[svmId]->setText(QString("%1: %2").arg(directionOrEventType).arg(details));
            if (msgName.contains("Fail", Qt::CaseInsensitive) || msgName.contains("Error", Qt::CaseInsensitive) || msgName.contains("Timeout", Qt::CaseInsensitive)) {
                m_errorDisplays[svmId]->setStyleSheet("background-color: lightcoral; color: white; font-style: italic;");
            } else if (msgName.contains("Warning", Qt::CaseInsensitive)) {
                m_errorDisplays[svmId]->setStyleSheet("background-color: yellow; color: black; font-style: italic;");
            } else { // Например, LinkStatus OK
                 m_errorDisplays[svmId]->setStyleSheet("color: green; font-style: italic;");
            }
        }
        // Не сбрасываем errorDisplay при обычных SENT/RECV, чтобы последняя ошибка была видна
    }

    table->scrollToBottom();
    if (table->rowCount() > 200) { // Ограничиваем историю
        table->removeRow(0);
    }
}

void MainWindow::updateSvmLinkStatusDisplay(int svmId, int newStatus)
{
    if (svmId < 0 || svmId >= MAX_GUI_SVM_INSTANCES || !m_statusLabels[svmId] || !m_errorDisplays[svmId]) return;

    m_statusLabels[svmId]->setText(statusToString(newStatus));
    m_statusLabels[svmId]->setStyleSheet(statusToStyleSheet(newStatus));

    // Обновляем errorDisplay на основе статуса
    if (newStatus == 2 /*UVM_LINK_ACTIVE*/) {
        m_errorDisplays[svmId]->setText("Status: OK");
        m_errorDisplays[svmId]->setStyleSheet("color: green; font-style: italic;");
    } else if (newStatus == 3 /*UVM_LINK_FAILED*/) {
        m_errorDisplays[svmId]->setText("Status: FAILED");
        m_errorDisplays[svmId]->setStyleSheet("background-color: lightcoral; color: white; font-style: italic;");
    } else if (newStatus == 0 /*UVM_LINK_INACTIVE*/) {
        m_errorDisplays[svmId]->setText("Status: INACTIVE");
        m_errorDisplays[svmId]->setStyleSheet("color: gray; font-style: italic;");
    }
    // Для других статусов можно добавить свои сообщения
}

void MainWindow::updateConnectionStatus(bool connected, const QString &message)
{
     ui->statusbar->showMessage(message);
     if (!connected) {
         for(int i = 0; i < MAX_GUI_SVM_INSTANCES; ++i) {
            updateSvmLinkStatusDisplay(i, 0); // 0 = UVM_LINK_INACTIVE
            if (m_lakLabels[i]) m_lakLabels[i]->setText("N/A");
            if (m_bcbLabels[i]) m_bcbLabels[i]->setText("N/A");
            if (m_errorDisplays[i]) m_errorDisplays[i]->setText("Disconnected from UVM App");
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

void MainWindow::onSaveLogAllClicked()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("Выберите директорию для сохранения логов"));
    if (dir.isEmpty()) return;

    for (int i=0; i < MAX_GUI_SVM_INSTANCES; ++i) {
        if (m_logTables[i] && m_logTables[i]->rowCount() > 0) {
             QString filename = dir + QString("/svm_%1_log.txt").arg(i);
             QFile file(filename);
             if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                 QTextStream out(&file);
                 out << "Log for SVM " << i << " (LAK: " << (m_lakLabels[i] ? m_lakLabels[i]->text() : "N/A") << ")\n";
                 out << "-------------------------------------------------------------------------------------------------\n";
                 // Заголовки таблицы
                 for (int col = 0; col < m_logTables[i]->columnCount(); ++col) {
                     out << m_logTables[i]->horizontalHeaderItem(col)->text() << (col == m_logTables[i]->columnCount()-1 ? "" : "\t|\t");
                 }
                 out << "\n";
                 out << "-------------------------------------------------------------------------------------------------\n";

                 for (int row = 0; row < m_logTables[i]->rowCount(); ++row) {
                     for (int col = 0; col < m_logTables[i]->columnCount(); ++col) {
                         QTableWidgetItem *item = m_logTables[i]->item(row, col);
                         out << (item ? item->text() : "") << (col == m_logTables[i]->columnCount()-1 ? "" : "\t|\t");
                     }
                     out << "\n";
                 }
                 file.close();
                 qDebug() << "Log for SVM" << i << "saved to" << filename;
             } else {
                 qWarning() << "Failed to open file for SVM" << i << ":" << filename;
             }
        }
    }
     ui->statusbar->showMessage("Logs saved to directory: " + dir, 5000);
}

void MainWindow::saveTableLogToFile(int svmId, const QString& baseDir) {
    if (svmId < 0 || svmId >= MAX_GUI_SVM_INSTANCES || !m_logTables[svmId] || m_logTables[svmId]->rowCount() == 0) {
        return;
    }

    QString filename = baseDir + QString("/svm_%1_log_%2.txt")
                           .arg(svmId)
                           .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        QString lakText = m_lakLabels[svmId] ? m_lakLabels[svmId]->text() : "N/A";
        out << "Log for SVM " << svmId << " (LAK: " << lakText << ")\n";
        out << "========================================================================================================================\n";

        // Заголовки таблицы
        for (int col = 0; col < m_logTables[svmId]->columnCount(); ++col) {
            out << m_logTables[svmId]->horizontalHeaderItem(col)->text()
                << (col == m_logTables[svmId]->columnCount()-1 ? "" : "\t|\t");
        }
        out << "\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";

        // Данные
        for (int row = 0; row < m_logTables[svmId]->rowCount(); ++row) {
            for (int col = 0; col < m_logTables[svmId]->columnCount(); ++col) {
                QTableWidgetItem *item = m_logTables[svmId]->item(row, col);
                out << (item ? item->text().replace("\n", " ") : "") // Заменяем переводы строк в деталях
                    << (col == m_logTables[svmId]->columnCount()-1 ? "" : "\t|\t");
            }
            out << "\n";
        }
        file.close();
        qDebug() << "Log for SVM" << svmId << "saved to" << filename;
        ui->statusbar->showMessage(QString("Log for SVM %1 saved.").arg(svmId), 3000);
    } else {
        qWarning() << "Failed to open file for SVM" << svmId << ":" << filename << "Error:" << file.errorString();
        ui->statusbar->showMessage(QString("Failed to save log for SVM %1!").arg(svmId), 5000);
    }
}