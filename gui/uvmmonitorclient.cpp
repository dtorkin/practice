#include "uvmmonitorclient.h"
#include <QDebug>
#include <QStringList>
#include <QDateTime>

// --- Конструктор и базовые функции соединения (без изменений) ---
UvmMonitorClient::UvmMonitorClient(QObject *parent)
    : QObject{parent}
{
    m_socket = new QTcpSocket(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(5000); // Пытаться переподключиться каждые 5 секунд

    connect(m_socket, &QTcpSocket::connected, this, &UvmMonitorClient::onConnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, &UvmMonitorClient::onErrorOccurred);
    connect(m_socket, &QTcpSocket::disconnected, this, &UvmMonitorClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &UvmMonitorClient::onReadyRead);
    connect(m_reconnectTimer, &QTimer::timeout, this, &UvmMonitorClient::attemptConnection);
}

UvmMonitorClient::~UvmMonitorClient() {}

void UvmMonitorClient::connectToServer(const QString &host, quint16 port) {
    m_host = host; m_port = port;
    qDebug() << "Attempting to connect to UVM server at" << m_host << ":" << m_port;
    m_reconnectTimer->stop();
    if (m_socket->state() == QAbstractSocket::UnconnectedState || m_socket->state() == QAbstractSocket::ClosingState) {
         m_buffer.clear();
        m_socket->connectToHost(m_host, m_port);
    } else if(m_socket->state() == QAbstractSocket::ConnectedState) {
        emit connectionStatusChanged(true, "Already connected to UVM.");
    }
}

void UvmMonitorClient::disconnectFromServer() {
    m_reconnectTimer->stop();
    if(m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

void UvmMonitorClient::onConnected() {
    qDebug() << "Connected to UVM server.";
    m_reconnectTimer->stop();
    emit connectionStatusChanged(true, "Connected to UVM");
}

void UvmMonitorClient::onDisconnected() {
    qDebug() << "Disconnected from UVM server.";
    emit connectionStatusChanged(false, "Disconnected from UVM. Retrying...");
    if (!m_reconnectTimer->isActive()) { m_reconnectTimer->start(); }
}

void UvmMonitorClient::onErrorOccurred(QAbstractSocket::SocketError socketError) {
    qWarning() << "Socket error:" << socketError << m_socket->errorString();
    emit connectionStatusChanged(false, "Error: " + m_socket->errorString() + ". Retrying...");
     if (m_socket->state() == QAbstractSocket::UnconnectedState && !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
     }
}

void UvmMonitorClient::attemptConnection() {
     qDebug() << "Attempting to reconnect...";
     if (m_socket->state() == QAbstractSocket::UnconnectedState) {
        connectToServer(m_host, m_port);
     }
}

void UvmMonitorClient::onReadyRead() {
    m_buffer.append(m_socket->readAll());
    while (m_buffer.contains('\n')) {
        int newlinePos = m_buffer.indexOf('\n');
        QByteArray lineData = m_buffer.left(newlinePos);
        m_buffer.remove(0, newlinePos + 1);
        parseData(lineData);
    }
}
// --- Конец базовых функций ---

QString UvmMonitorClient::getMessageNameByType(int type) {
    // ... (функция getMessageNameByType как в предыдущем ответе) ...
    switch (type) {
        case 128: return "Инициализация канала";
        case 1:   return "Провести контроль";
        case 2:   return "Выдать результаты контроля";
        case 6:   return "Выдать состояние линии";
        case 160: return "Принять параметры СО";
        case 161: return "Принять TIME_REF_RANGE";
        case 162: return "Принять Reper";
        case 170: return "Принять параметры СДР";
        case 200: return "Принять параметры 3ЦО";
        case 201: return "Принять REF_AZIMUTH";
        case 210: return "Принять параметры ЦДР";
        case 255: return "Навигационные данные";
        case 129: return "Подтверждение инициализации";
        case 3:   return "Подтверждение контроля";
        case 4:   return "Результаты контроля";
        case 7:   return "Состояние линии";
        case 127: return "СУБК";
        case 137: return "КО";
        case 8:   return "Строка голограммы СУБК";
        case 18:  return "Строка радиоголограммы ДР";
        case 19:  return "Строка К3";
        case 20:  return "Строка изображения К4";
        case 80:  return "НК";
        case 81:  return "Помеха";
        case 82:  return "Результат ОР1";
        case 84:  return "РО";
        case 90:  return "НКДР";
        case 254: return "Предупреждение";
        default:  return QString("Unknown (%1)").arg(type);
    }
}

void UvmMonitorClient::parseData(const QByteArray& data)
{
     QString line = QString::fromUtf8(data).trimmed();
     if (line.isEmpty()) return;

     //qDebug() << "IPC Raw:" << line;

     QStringList parts = line.split(';');
     if (parts.isEmpty()) {
         qWarning() << "IPC: Empty line after split:" << line;
         return;
     }

     QString eventDirectionOrType = parts[0].trimmed(); // SENT, RECV, EVENT
     int svmId = -1;
     int msgType = -1;
     QString msgName = "N/A";
     int msgNum = -1;
	 quint32 bcbFromIPC = 0;
     int lak = -1; // LAK, пришедший в строке (может быть LAK SVM для SENT или LAK UVM для RECV)
	 int weightFromIPC = 0;
     QString detailsStr = "";
     QDateTime timestamp = QDateTime::currentDateTime();

     QMap<QString, QString> fieldsMap;
     for (int i = 1; i < parts.size(); ++i) {
         QString part = parts[i].trimmed();
         if (part.isEmpty()) continue; // Пропускаем пустые части

         int colonPos = part.indexOf(':');
         if (colonPos > 0) { // Убедимся, что ':' не первый символ и есть
             QString key = part.left(colonPos).trimmed();
             QString value = part.mid(colonPos + 1).trimmed();
             fieldsMap.insert(key, value);
         } else if (i == parts.size() - 1 && eventDirectionOrType == "EVENT") {
             // Если это последний элемент в EVENT и нет ':', считаем его деталями
             fieldsMap.insert("Details", part);
         } else {
             qWarning() << "IPC: Malformed field part:" << part << "in line:" << line;
         }
     }

     bool ok;
     if (fieldsMap.contains("SVM_ID")) {
         svmId = fieldsMap.value("SVM_ID").toInt(&ok);
         if (!ok) { qWarning() << "IPC: Failed to parse SVM_ID:" << fieldsMap.value("SVM_ID"); return; }
     } else {
         qWarning() << "IPC: SVM_ID field missing:" << line; return;
     }

     if (fieldsMap.contains("Type")) {
         msgType = fieldsMap.value("Type").toInt(&ok);
         if (!ok && eventDirectionOrType != "EVENT") { // Для EVENT msgType может быть строкой
             qWarning() << "IPC: Failed to parse MsgType for SVM" << svmId << ":" << fieldsMap.value("Type");
             msgType = -1;
         }
     }
     if (fieldsMap.contains("Num")) {
         msgNum = fieldsMap.value("Num").toInt(&ok);
         if (!ok) msgNum = -1;
     }
     if (fieldsMap.contains("LAK")) {
         QString lakStr = fieldsMap.value("LAK");
         // toUInt с базой 0 автоматически определяет 0x
         lak = lakStr.toUInt(&ok, 0);
         if (!ok) {
             qWarning() << "IPC: Failed to parse LAK for SVM" << svmId << ":" << lakStr;
             lak = -1;
         }
     }
	 
     if (fieldsMap.contains("BCB")) {
         bool ok_bcb;
         bcbFromIPC = fieldsMap.value("BCB").toUInt(&ok_bcb, 0); // Парсим как hex/dec
         if (!ok_bcb) {
             qWarning() << "IPC: Failed to parse BCB for SVM" << svmId << ":" << fieldsMap.value("BCB");
             bcbFromIPC = 0; // Сброс при ошибке
         }
     }
     if (fieldsMap.contains("Details")) {
         detailsStr = fieldsMap.value("Details");
     }
	 
    if (fieldsMap.contains("Weight")) { // <--- ИЗВЛЕКАЕМ ПОЛЕ WEIGHT
        bool ok_weight;
        weightFromIPC = fieldsMap.value("Weight").toInt(&ok_weight);
        if (!ok_weight) {
            qWarning() << "IPC: Failed to parse Weight for SVM" << svmId << ":" << fieldsMap.value("Weight");
            weightFromIPC = 0; // Сброс при ошибке
        }
    }

     if (eventDirectionOrType == "SENT" || eventDirectionOrType == "RECV") {
         if (msgType != -1) {
             msgName = getMessageNameByType(msgType);
         } else {
             msgName = "Invalid Type";
         }
     } else if (eventDirectionOrType == "EVENT") {
         // Для EVENT, "Type" из fieldsMap - это имя события
         if (fieldsMap.contains("Type")) {
             msgName = fieldsMap.value("Type");
         } else {
             msgName = "Unknown Event";
         }
         // msgType для EVENT не используется для имени, msgNum тоже
         msgType = -1; // Установим в невалидное значение для EVENT
         msgNum = -1;
     } else {
         qWarning() << "IPC: Unknown message main type:" << eventDirectionOrType << "Full line:" << line;
         return;
     }

     // Для SENT/RECV передаем lak из строки. Для EVENT, lak нерелевантен в этом сигнале.
     // AssignedLAK будет передан через svmLinkStatusChanged.
    emit newMessageOrEvent(svmId, timestamp, eventDirectionOrType, msgType, msgName, msgNum,
                           lak, // LAK из строки IPC
                           (eventDirectionOrType == "SENT" || eventDirectionOrType == "RECV") ? bcbFromIPC : 0, // Передаем BCB для SENT/RECV, 0 для EVENT
						   weightFromIPC,
                           detailsStr);

     // Отдельный сигнал для LinkStatus, если он был передан как EVENT
     if(eventDirectionOrType == "EVENT" && msgName == "LinkStatus") {
         int newStatus = -1;
         int assignedLakFromEvent = -1; // Это LAK самого SVM
         QStringList detailParts = detailsStr.split(',');
         for(const QString& part : detailParts) {
             QStringList kv = part.split('=');
             if (kv.size() == 2) {
                 QString key = kv[0].trimmed();
                 QString val = kv[1].trimmed();
                 if (key == "NewStatus") {
                     newStatus = val.toInt(&ok);
                     if (!ok) newStatus = -1;
                 } else if (key == "AssignedLAK") {
                     assignedLakFromEvent = val.toUInt(&ok, 0); // Парсим как hex/dec
                     if (!ok) assignedLakFromEvent = -1;
                 }
             }
         }
         if (newStatus != -1) {
             emit svmLinkStatusChanged(svmId, newStatus, assignedLakFromEvent);
         }
     }
}