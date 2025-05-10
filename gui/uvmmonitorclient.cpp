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

     QString directionOrEventType = parts[0].trimmed();
     int svmId = -1;
     int msgType = -1;
     QString msgName = "N/A";
     int msgNum = -1;
     int lak = -1;
     QString detailsStr = ""; // Для EVENT details или для SENT/RECV payload info
     QDateTime timestamp = QDateTime::currentDateTime();

     QMap<QString, QString> fieldsMap;
     for (int i = 1; i < parts.size(); ++i) {
         QStringList key_value = parts[i].split(':', Qt::SkipEmptyParts); // Qt6: QString::SkipEmptyParts
         if (key_value.size() == 2) {
             fieldsMap.insert(key_value[0].trimmed(), key_value[1].trimmed());
         } else if (!parts[i].trimmed().isEmpty()){
             // Если это последний элемент и он не содержит ':', это могут быть Details
             if (i == parts.size() -1 && !parts[i].contains(':')) {
                 fieldsMap.insert("Details", parts[i].trimmed()); // Предполагаем, что это детали
             } else {
                 qWarning() << "IPC: Malformed field:" << parts[i] << "in line:" << line;
             }
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
         if (!ok) msgType = -1; // Ошибка парсинга
     }
     if (fieldsMap.contains("Num")) {
         msgNum = fieldsMap.value("Num").toInt(&ok);
         if (!ok) msgNum = -1;
     }
     if (fieldsMap.contains("LAK")) {
         lak = fieldsMap.value("LAK").toUInt(&ok, 0); // 0 для автоопределения (0x)
         if (!ok) lak = -1;
     }
     if (fieldsMap.contains("Details")) {
         detailsStr = fieldsMap.value("Details");
     }

     if (msgType != -1 && (directionOrEventType == "SENT" || directionOrEventType == "RECV")) {
         msgName = getMessageNameByType(msgType);
     } else if (directionOrEventType == "EVENT" && msgType != -1) { // Для EVENT, msgType - это код статуса/ошибки
         msgName = fieldsMap.value("Type"); // Имя события уже в msgName
     }


     emit newMessageOrEvent(svmId, timestamp, directionOrEventType, msgType, msgName, msgNum, lak, detailsStr);

     // Отдельный сигнал для LinkStatus, если он был передан как EVENT
     if(directionOrEventType == "EVENT" && msgName == "LinkStatus") {
         // Details для LinkStatus ожидается как "NewStatus=X,AssignedLAK=Y"
         int newStatus = -1;
         int assignedLakFromEvent = -1;
         QStringList detailParts = detailsStr.split(',');
         for(const QString& part : detailParts) {
             QStringList kv = part.split('=');
             if (kv.size() == 2) {
                 if (kv[0].trimmed() == "NewStatus") {
                     newStatus = kv[1].trimmed().toInt(&ok);
                     if (!ok) newStatus = -1;
                 } else if (kv[0].trimmed() == "AssignedLAK") {
                     assignedLakFromEvent = kv[1].trimmed().toUInt(&ok, 0);
                     if (!ok) assignedLakFromEvent = -1;
                 }
             }
         }
         if (newStatus != -1) {
             emit svmLinkStatusChanged(svmId, newStatus, assignedLakFromEvent);
         }
     }
}