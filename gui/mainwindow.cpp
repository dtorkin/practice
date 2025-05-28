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
    table->setColumnCount(9);
    QStringList headers = {"Время", "Напр/Соб.", "LAK SVM", "BCB", "Вес, Б", "Тип сообщ.", "Имя сообщ.", "Номер сообщ.", "Детали"}; 
    table->setHorizontalHeaderLabels(headers);
    table->horizontalHeader()->setStretchLastSection(true); // Последний столбец (Детали) растягивается
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
	table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setWordWrap(false); // Отключаем автоперенос в ячейках для краткости

    // Примерная настройка ширины столбцов
    table->setColumnWidth(0, 100); // Время
    table->setColumnWidth(1, 80);  // Напр/Соб.
    table->setColumnWidth(2, 70);  // LAK SVM
    table->setColumnWidth(3, 80);  // BCB
    table->setColumnWidth(4, 60);  // Вес, Б <--- НОВЫЙ СТОЛБЕЦ
    table->setColumnWidth(5, 50);  // Тип сообщ. (сдвинулся)
    table->setColumnWidth(6, 180); // Имя сообщ. (сдвинулся, можно уменьшить если нужно)
    table->setColumnWidth(7, 70);  // Номер сообщ. (сдвинулся)
    // table->setColumnWidth(8, ...); // Детали - растягивается (сдвинулся)
    table->verticalHeader()->setVisible(false); // Скрыть нумерацию строк слева
}

void MainWindow::onNewMessageOrEvent(int svmId, const QDateTime &timestamp, const QString &directionOrEventType,
                                   int msgType, const QString &msgName, int msgNum,
                                   int lakInIPCMessage, // Назовите одинаково с .h
                                   quint32 bcb,       // <--- ИЗМЕНИТЕ ЗДЕСЬ ИМЯ АРГУМЕНТА НА "bcb"
								   int weight,
                                   const QString &details) {
    if (svmId < 0 || svmId >= MAX_GUI_SVM_INSTANCES || !(m_logTables.size() > svmId && m_logTables[svmId])) {
        qWarning() << "Received message/event for invalid SVM ID or uninitialized table:" << svmId;
        return;
    }

    QTableWidget *table = m_logTables[svmId];
    int row = table->rowCount();
    table->insertRow(row);

    // 1. Время
    table->setItem(row, 0, new QTableWidgetItem(timestamp.toString("hh:mm:ss.zzz")));

    // 2. Направление/Событие
    table->setItem(row, 1, new QTableWidgetItem(directionOrEventType));

    // 3. LAK SVM (используем сохраненный "родной" LAK экземпляра)
    int displayLak = -1;
    if (svmId >= 0 && svmId < m_assignedLaks.size() && m_assignedLaks[svmId] >= 0) {
        displayLak = m_assignedLaks[svmId];
    } else if (directionOrEventType == "SENT") {
        // Если "родной" LAK еще не известен, но это SENT, то lakInIPCMessage - это LAK SVM
        displayLak = lakInIPCMessage;
    }
    // Если это RECV и "родной" LAK не известен, то lakInIPCMessage будет адресом UVM (0x01),
    // что тоже можно отобразить или оставить N/A. Пока оставляем так.
    table->setItem(row, 2, new QTableWidgetItem((displayLak >=0) ? QString("0x%1").arg(displayLak, 2, 16, QChar('0')).toUpper() : "N/A"));

    // 4. BCB
    if (directionOrEventType == "SENT" || directionOrEventType == "RECV") {
        if (bcb != 0) {
            table->setItem(row, 3, new QTableWidgetItem(QString("0x%1").arg(bcb, 8, 16, QChar('0')).toUpper()));
        } else {
            table->setItem(row, 3, new QTableWidgetItem("-"));
        }
        // 5. Вес 
        if (weight > 0) {
            table->setItem(row, 4, new QTableWidgetItem(QString::number(weight)));
        } else {
            table->setItem(row, 4, new QTableWidgetItem("-"));
        }
        // 6. Тип сообщения
        table->setItem(row, 5, new QTableWidgetItem(QString::number(msgType)));
        // 7. Имя сообщения
        table->setItem(row, 6, new QTableWidgetItem(msgName));
        // 8. Номер сообщения
        table->setItem(row, 7, new QTableWidgetItem(QString::number(msgNum)));
        // 9. Детали
        table->setItem(row, 8, new QTableWidgetItem(details));
    } else { // EVENT
        table->setItem(row, 3, new QTableWidgetItem("-")); // BCB для EVENT
        table->setItem(row, 4, new QTableWidgetItem("-")); // Вес для EVENT
        table->setItem(row, 5, new QTableWidgetItem("-")); // Тип
        table->setItem(row, 6, new QTableWidgetItem(msgName)); // Имя события
        table->setItem(row, 7, new QTableWidgetItem("-")); // Номер
        table->setItem(row, 8, new QTableWidgetItem(details)); // Детали
    }

    // Обновляем основной LAK QLabel (это делает updateSvmLinkStatusDisplay, когда LAK становится известен)
    // Здесь можно было бы сделать, если LAK обновился из SENT, но лучше централизовать в updateSvmLinkStatusDisplay

    // Обновляем errorDisplay
    if (m_errorDisplays.size() > svmId && m_errorDisplays[svmId]) {
        if (directionOrEventType == "EVENT") {
            QString eventText = QString("%1: %2").arg(msgName).arg(details); // msgName здесь = тип события
            m_errorDisplays[svmId]->setText(eventText);

            if (msgName.contains("Fail", Qt::CaseInsensitive) ||
                msgName.contains("Error", Qt::CaseInsensitive) ||
                msgName.contains("Timeout", Qt::CaseInsensitive) ||
                msgName.contains("Mismatch", Qt::CaseInsensitive)) {
                m_errorDisplays[svmId]->setStyleSheet("background-color: lightcoral; color: white; font-style: italic; padding: 2px;");
            } else if (msgName.contains("Warning", Qt::CaseInsensitive)) {
                m_errorDisplays[svmId]->setStyleSheet("background-color: #FFFACD; color: black; font-style: italic; padding: 2px;"); // LemonChiffon
            } else if (msgName == "LinkStatus" && details.contains("NewStatus=2")) { // 2 = ACTIVE
                 // Сбрасываем на ОК, только если это событие LinkStatus=ACTIVE
                 m_errorDisplays[svmId]->setText("Status: OK");
                 m_errorDisplays[svmId]->setStyleSheet("color: green; font-style: normal;");
            } else { // Другие информационные события
                 m_errorDisplays[svmId]->setStyleSheet("color: blue; font-style: italic;");
            }
        }
        // При обычных SENT/RECV сообщениях, errorDisplay НЕ сбрасывается на "OK",
        // чтобы последнее важное событие/ошибка оставалось видимым.
        // Сброс на "OK" происходит только при получении события "LinkStatus" с активным статусом.
    }

	// Цвет
    QColor rowColor;
    if (directionOrEventType == "SENT") {
        rowColor = QColor(220, 230, 255); // Еще светлее голубой для SENT (пример)
    } else if (directionOrEventType == "RECV") {
        rowColor = QColor(220, 255, 220); // Еще светлее зеленый для RECV (пример)
    } else if (directionOrEventType == "EVENT") {
        // msgName для EVENT содержит тип события (LinkStatus, ControlFail, и т.д.)
        // details может содержать дополнительную информацию, включая RSK, TKS
        bool isError = msgName.contains("Fail", Qt::CaseInsensitive) ||
                       msgName.contains("Error", Qt::CaseInsensitive) ||
                       msgName.contains("Timeout", Qt::CaseInsensitive) ||
                       msgName.contains("Mismatch", Qt::CaseInsensitive) ||
                       (msgName == "LinkStatus" && details.contains("NewStatus=3")); // 3 = FAILED

        bool isWarning = msgName.contains("Warning", Qt::CaseInsensitive) ||
                         (msgName == "LinkStatus" && details.contains("NewStatus=5")); // 5 = WARNING

        if (isError) {
            rowColor = QColor(255, 200, 200); // Светло-красный для ошибок/фейлов
        } else if (isWarning) {
            rowColor = QColor(255, 240, 180); // Светло-оранжевый/желтый для предупреждений
        } else if (msgName == "LinkStatus" && details.contains("NewStatus=2")) { // 2 = ACTIVE
             rowColor = QColor(200, 255, 200); // Светло-зеленый для LinkStatus=ACTIVE (как RECV)
        }
        else {
            rowColor = QColor(235, 235, 235); // Светло-серый для других событий
        }
    } else {
        rowColor = Qt::white; // По умолчанию (хотя такого случая быть не должно)
    }

    for (int col = 0; col < table->columnCount(); ++col) {
        QTableWidgetItem *item = table->item(row, col);
        if (item) { 
            item->setBackground(rowColor);
        }
    }

	// Листание вниз
    table->scrollToBottom();
    if (table->rowCount() > 200) { // Ограничиваем историю в таблице
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