#include "mainwindow.h"
#include "ui_mainwindow.h" // Генерируется из mainwindow.ui
#include <QTimer>
#include <QDebug>
#include <QTableWidget>
#include <QPushButton>
#include <QFileDialog>
#include <QFile>
#include <QTextStream>
#include <QHeaderView>

// Константа для максимального количества отображаемых SVM (должна совпадать с MAX_GUI_SVM_INSTANCES в C-коде)
const int MAX_GUI_SVM_INSTANCES = 4;

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_assignedLaks(MAX_GUI_SVM_INSTANCES, -1) // Инициализируем LAKи значением "неизвестно"
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
         // Проверяем, что виджеты действительно существуют (если они созданы в .ui)
         if (i < m_statusLabels.size() && m_statusLabels[i]) {
             m_statusLabels[i]->setText(statusToString(0)); // UVM_LINK_INACTIVE
             m_statusLabels[i]->setStyleSheet(statusToStyleSheet(0));
         }
         if (i < m_lakLabels.size() && m_lakLabels[i]) m_lakLabels[i]->setText("N/A");
         if (i < m_bcbLabels.size() && m_bcbLabels[i]) m_bcbLabels[i]->setText("N/A");
         if (i < m_errorDisplays.size() && m_errorDisplays[i]) {
             m_errorDisplays[i]->setText("Status: OK");
             m_errorDisplays[i]->setStyleSheet("color: green; font-style: italic;");
         }
         if (i < m_logTables.size() && m_logTables[i]) {
             initTableWidget(m_logTables[i]);
         }
    }

    // Подключаем сигнал от кнопки сохранения (предполагаем, что она есть в ui с objectName="buttonSaveAllLogs")
    if (ui->buttonSaveAllLogs) {
        connect(ui->buttonSaveAllLogs, &QPushButton::clicked, this, &MainWindow::onSaveLogAllClicked);
    } else {
        qWarning() << "Button 'buttonSaveAllLogs' not found in UI. Save functionality will not be available via button.";
    }


    m_client = new UvmMonitorClient(this);
    connect(m_client, &UvmMonitorClient::newMessageOrEvent, this, &MainWindow::onNewMessageOrEvent);
    connect(m_client, &UvmMonitorClient::connectionStatusChanged, this, &MainWindow::updateConnectionStatus);
    connect(m_client, &UvmMonitorClient::svmLinkStatusChanged, this, &MainWindow::updateSvmLinkStatusDisplay);

    // Подключаемся к UVM App
    m_client->connectToServer("127.0.0.1", 12345); // Хост и порт для IPC с uvm_app
    ui->statusbar->showMessage("Attempting to connect to UVM App on localhost:12345...");
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::initTableWidget(QTableWidget* table) {
    if (!table) return;
    table->setColumnCount(7);
    QStringList headers = {"Время", "Напр/Соб.", "LAK SVM", "Тип сообщ.", "Имя сообщ.", "Номер сообщ.", "Детали"};
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true); // Последний столбец (Детали) растягивается
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setWordWrap(false); // Отключаем автоперенос в ячейках для краткости

    // Примерная настройка ширины столбцов
    table->setColumnWidth(0, 100); // Время (hh:mm:ss.zzz)
    table->setColumnWidth(1, 80);  // Направление/Событие (SENT, RECV, EVENT)
    table->setColumnWidth(2, 70);  // LAK SVM
    table->setColumnWidth(3, 50);  // Тип сообщения (число)
    table->setColumnWidth(4, 200); // Имя сообщения
    table->setColumnWidth(5, 70);  // Номер сообщения
    // table->setColumnWidth(6, 250); // Детали - будет растягиваться
    table->verticalHeader()->setVisible(false); // Скрыть нумерацию строк слева
}

void MainWindow::onNewMessageOrEvent(int svmId, const QDateTime ×tamp, const QString &directionOrEventType,
                                   int msgType, const QString &msgName, int msgNum,
                                   int lakFromIPC, const QString &details) // lakFromIPC - это LAK, пришедший из IPC строки
{
    if (svmId < 0 || svmId >= MAX_GUI_SVM_INSTANCES || !(m_logTables.size() > svmId && m_logTables[svmId])) {
        qWarning() << "Received message/event for invalid SVM ID or uninitialized table:" << svmId;
        return;
    }

    QTableWidget *table = m_logTables[svmId];
    int row = table->rowCount();
    table->insertRow(row);

    table->setItem(row, 0, new QTableWidgetItem(timestamp.toString("hh:mm:ss.zzz")));
    table->setItem(row, 1, new QTableWidgetItem(directionOrEventType));

    // --- ИЗМЕНЕНИЕ: Отображаем "родной" LAK SVM ---
    int displayLak = -1;
    if (svmId >= 0 && svmId < m_assignedLaks.size() && m_assignedLaks[svmId] >= 0) {
        displayLak = m_assignedLaks[svmId]; // Используем сохраненный "родной" LAK
    } else if (directionOrEventType == "SENT") {
        displayLak = lakFromIPC; // Для SENT это и есть LAK SVM
    } else {
        // Для RECV или EVENT без сохраненного LAK, можно показать LAK из IPC (который будет UVM для RECV)
        // или оставить N/A, или показать LAK из деталей, если он там есть
        displayLak = lakFromIPC; // Оставляем LAK из IPC, если "родной" еще не известен
    }
    table->setItem(row, 2, new QTableWidgetItem((displayLak >=0) ? QString("0x%1").arg(displayLak, 2, 16, QChar('0')).toUpper() : "N/A"));
    // --- КОНЕЦ ИЗМЕНЕНИЯ ---


    QString detailsToShow = details;

    if (directionOrEventType == "SENT" || directionOrEventType == "RECV") {
        table->setItem(row, 3, new QTableWidgetItem(QString::number(msgType)));
        table->setItem(row, 4, new QTableWidgetItem(msgName));
        table->setItem(row, 5, new QTableWidgetItem(QString::number(msgNum)));

        // Обновляем отдельный QLabel для BCB, если детали содержат BCB
        if (m_bcbLabels.size() > svmId && m_bcbLabels[svmId] && !details.isEmpty()) {
            // Ищем "BCB=значение" в строке деталей
            QRegExp bcbRegex("BCB=([0-9A-Fa-fXx]+)"); // Регулярное выражение для BCB (hex или dec)
            int pos = bcbRegex.indexIn(details);
            if (pos != -1) {
                bool ok;
                quint32 bcbVal = bcbRegex.cap(1).toUInt(&ok, 0); // cap(1) - значение из скобок, 0 для автоопределения hex
                if (ok && bcbVal != m_lastDisplayedBcb[svmId]) {
                    m_bcbLabels[svmId]->setText(QString::number(bcbVal));
                    m_lastDisplayedBcb[svmId] = bcbVal;
                }
            }
        }
        // Для SENT/RECV details могут содержать и другую информацию (RSK, TKS и т.д.)
        detailsToShow = details; // Отображаем все детали
    } else { // EVENT
        table->setItem(row, 3, new QTableWidgetItem("-"));
        table->setItem(row, 4, new QTableWidgetItem(msgName)); // msgName здесь - это тип события
        table->setItem(row, 5, new QTableWidgetItem("-"));
        detailsToShow = details; // Для EVENT детали всегда отображаем
    }
    table->setItem(row, 6, new QTableWidgetItem(detailsToShow));


    // Обновляем основной LAK QLabel, если он еще не установлен или изменился (из LinkStatus)
    // Это делается в updateSvmLinkStatusDisplay, здесь дублировать не обязательно,
    // но можно оставить для случая, если первое сообщение - не LinkStatus.
    if (m_lakLabels.size() > svmId && m_lakLabels[svmId] && displayLak >=0) {
         if (m_lakLabels[svmId]->text() == "N/A" || m_lakLabels[svmId]->text() != QString("0x%1").arg(displayLak, 2, 16, QChar('0')).toUpper()) {
            if (m_assignedLaks[svmId] == displayLak) { // Обновляем, только если LAK совпадает с назначенным
                m_lakLabels[svmId]->setText(QString("0x%1").arg(displayLak, 2, 16, QChar('0')).toUpper());
            }
         }
    }

    // Обновляем errorDisplay
    if (m_errorDisplays.size() > svmId && m_errorDisplays[svmId]) {
        if (directionOrEventType == "EVENT") {
            QString eventText = QString("%1: %2").arg(msgName).arg(details);
            m_errorDisplays[svmId]->setText(eventText);
            // Установка стиля для errorDisplay (как раньше)
            if (msgName.contains("Fail", Qt::CaseInsensitive) || /*...*/) { /*...*/ }
            else if (msgName.contains("Warning", Qt::CaseInsensitive)) { /*...*/ }
            else if (msgName == "LinkStatus" && details.contains("NewStatus=2")) { /*...*/ } // 2 = ACTIVE
            else { /*...*/ }
        }
        // Не сбрасываем errorDisplay при обычных SENT/RECV
    }

    table->scrollToBottom();
    if (table->rowCount() > 200) {
        table->removeRow(0);
    }
}

void MainWindow::updateSvmLinkStatusDisplay(int svmId, int newStatus, int assignedLakFromEvent)
{
    if (svmId < 0 || svmId >= MAX_GUI_SVM_INSTANCES ) return;

    if (m_statusLabels.size() > svmId && m_statusLabels[svmId]) {
        m_statusLabels[svmId]->setText(statusToString(newStatus));
        m_statusLabels[svmId]->setStyleSheet(statusToStyleSheet(newStatus));
    }
    if (m_lakLabels.size() > svmId && m_lakLabels[svmId] && assignedLakFromEvent >= 0) {
        m_lakLabels[svmId]->setText(QString("0x%1").arg(assignedLakFromEvent, 2, 16, QChar('0')).toUpper());
        m_assignedLaks[svmId] = assignedLakFromEvent; // Сохраняем для лога
    }

    // Обновляем errorDisplay на основе статуса, если нет более специфичного сообщения об ошибке
    if (m_errorDisplays.size() > svmId && m_errorDisplays[svmId]) {
        bool isErrorOrWarning = m_errorDisplays[svmId]->styleSheet().contains("lightcoral") ||
                                m_errorDisplays[svmId]->styleSheet().contains("yellow");

        if (newStatus == 2 /*UVM_LINK_ACTIVE*/ && !isErrorOrWarning) {
            m_errorDisplays[svmId]->setText("Status: OK");
            m_errorDisplays[svmId]->setStyleSheet("color: green; font-style: normal;");
        } else if (newStatus == 3 /*UVM_LINK_FAILED*/) {
            m_errorDisplays[svmId]->setText("Status: FAILED");
            m_errorDisplays[svmId]->setStyleSheet("background-color: lightcoral; color: white; font-style: italic; padding: 2px;");
        } else if (newStatus == 0 /*UVM_LINK_INACTIVE*/) {
            m_errorDisplays[svmId]->setText("Status: INACTIVE");
            m_errorDisplays[svmId]->setStyleSheet("color: gray; font-style: italic;");
        }
    }
}

void MainWindow::updateConnectionStatus(bool connected, const QString &message)
{
     ui->statusbar->showMessage(message);
     if (!connected) {
         for(int i = 0; i < MAX_GUI_SVM_INSTANCES; ++i) {
            // Вызываем updateSvmLinkStatusDisplay для сброса
            updateSvmLinkStatusDisplay(i, 0, m_assignedLaks[i] >=0 ? m_assignedLaks[i] : -1); // 0 = UVM_LINK_INACTIVE
            if (m_bcbLabels.size() > i && m_bcbLabels[i]) m_bcbLabels[i]->setText("N/A");
            if (m_errorDisplays.size() > i && m_errorDisplays[i]) {
                m_errorDisplays[i]->setText("UVM App Disconnected");
                m_errorDisplays[i]->setStyleSheet("color: red; font-weight: bold;");
            }
         }
     }
}

QString MainWindow::statusToString(int status) {
    switch (status) {
        case 0: return "INACTIVE";
        case 1: return "CONNECTING";
        case 2: return "ACTIVE";
        case 3: return "FAILED";
        case 4: return "DISCONNECTING";
        case 5: return "WARNING"; // UVM_LINK_WARNING
        default: return "UNKNOWN";
    }
}

QString MainWindow::statusToStyleSheet(int status) {
     QString style = "padding: 2px; color: black; font-weight: bold;";
     switch (status) {
         case 0: style += "background-color: lightgray;"; break;  // INACTIVE
         case 1: style += "background-color: #FFE4B5;"; break;    // CONNECTING (Moccasin)
         case 2: style += "background-color: lightgreen;"; break; // ACTIVE
         case 3: style += "background-color: lightcoral;"; break;  // FAILED
         case 4: style += "background-color: orange;"; break;     // DISCONNECTING
         case 5: style += "background-color: yellow;"; break;     // WARNING
         default: style += "background-color: white;"; break;     // UNKNOWN
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
    if (svmId < 0 || svmId >= MAX_GUI_SVM_INSTANCES || !(m_logTables.size() > svmId && m_logTables[svmId]) || m_logTables[svmId]->rowCount() == 0) {
        qDebug() << "No log data to save for SVM ID" << svmId;
        return;
    }

    QString lakText = (m_assignedLaks[svmId] >= 0) ? QString("0x%1").arg(m_assignedLaks[svmId], 2, 16, QChar('0')).toUpper() : "N/A";
    QString filename = baseDir + QString("/svm_%1_lak_%2_log_%3.txt")
                           .arg(svmId)
                           .arg(lakText.remove("0x")) // Убираем "0x" для имени файла
                           .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) { // Truncate чтобы перезаписать
        QTextStream out(&file);
        out.setCodec("UTF-8"); // Устанавливаем кодировку

        out << "Log for SVM " << svmId << " (Assigned LAK: " << lakText << ")\n";
        out << "========================================================================================================================\n";

        QStringList headers;
        for (int col = 0; col < m_logTables[svmId]->columnCount(); ++col) {
            headers << m_logTables[svmId]->horizontalHeaderItem(col)->text();
        }
        out << headers.join("\t|\t") << "\n";
        out << "------------------------------------------------------------------------------------------------------------------------\n";

        for (int row = 0; row < m_logTables[svmId]->rowCount(); ++row) {
            QStringList rowData;
            for (int col = 0; col < m_logTables[svmId]->columnCount(); ++col) {
                QTableWidgetItem *item = m_logTables[svmId]->item(row, col);
                rowData << (item ? item->text().replace("\n", " ") : "");
            }
            out << rowData.join("\t|\t") << "\n";
        }
        file.close();
        qDebug() << "Log for SVM" << svmId << "saved to" << filename;
        // ui->statusbar->showMessage(QString("Log for SVM %1 saved.").arg(svmId), 3000); // Будет перезаписано общим сообщением
    } else {
        qWarning() << "Failed to open file for SVM" << svmId << ":" << filename << "Error:" << file.errorString();
        ui->statusbar->showMessage(QString("Failed to save log for SVM %1!").arg(svmId), 5000);
    }
}