#include "uvmmonitorclient.h"
#include <QDebug>
#include <QStringList>
#include <QThread> // Для QThread::msleep (если понадобится)

UvmMonitorClient::UvmMonitorClient(QObject *parent)
    : QObject{parent}
{
    m_socket = new QTcpSocket(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(5000);

    connect(m_socket, &QTcpSocket::connected, this, &UvmMonitorClient::onConnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, &UvmMonitorClient::onErrorOccurred);
    connect(m_socket, &QTcpSocket::disconnected, this, &UvmMonitorClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &UvmMonitorClient::onReadyRead);
    connect(m_reconnectTimer, &QTimer::timeout, this, &UvmMonitorClient::attemptConnection);
}

UvmMonitorClient::~UvmMonitorClient()
{
    // Qt автоматически удалит дочерние объекты
}

void UvmMonitorClient::connectToServer(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    qDebug() << "Attempting to connect to UVM server at" << m_host << ":" << m_port;
    m_reconnectTimer->stop(); // Остановить таймер перед новой попыткой
    if (m_socket->state() == QAbstractSocket::UnconnectedState || m_socket->state() == QAbstractSocket::ClosingState) {
         m_buffer.clear(); // Очистить буфер при новом подключении
        m_socket->connectToHost(m_host, m_port);
    } else {
        qDebug() << "Socket is not in UnconnectedState:" << m_socket->state();
        if(m_socket->state() == QAbstractSocket::ConnectedState) {
            emit connectionStatusChanged(true, "Already connected to UVM.");
        }
    }
}

void UvmMonitorClient::disconnectFromServer()
{
    m_reconnectTimer->stop();
    if(m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->disconnectFromHost();
    }
}

void UvmMonitorClient::onConnected()
{
    qDebug() << "Connected to UVM server.";
    m_reconnectTimer->stop(); // Подключились, таймер не нужен
    emit connectionStatusChanged(true, "Connected to UVM");
}

void UvmMonitorClient::onDisconnected()
{
    qDebug() << "Disconnected from UVM server.";
    emit connectionStatusChanged(false, "Disconnected from UVM. Retrying...");
    if (!m_reconnectTimer->isActive()) { // Запускаем, только если еще не активен
        m_reconnectTimer->start();
    }
}

void UvmMonitorClient::onErrorOccurred(QAbstractSocket::SocketError socketError)
{
    qWarning() << "Socket error:" << socketError << m_socket->errorString();
    emit connectionStatusChanged(false, "Error: " + m_socket->errorString() + ". Retrying...");
     if (m_socket->state() == QAbstractSocket::UnconnectedState && !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
     }
}

void UvmMonitorClient::attemptConnection()
{
     qDebug() << "Attempting to reconnect...";
     if (m_socket->state() == QAbstractSocket::UnconnectedState) { // Проверяем перед попыткой
        connectToServer(m_host, m_port); // Используем сохраненные хост и порт
     }
}

void UvmMonitorClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());
    while (m_buffer.contains('\n')) {
        int newlinePos = m_buffer.indexOf('\n');
        QByteArray lineData = m_buffer.left(newlinePos);
        m_buffer.remove(0, newlinePos + 1);
        parseData(lineData);
    }
}

// Вспомогательная функция для получения имени сообщения по типу
QString UvmMonitorClient::getMessageNameByType(int type) {
    switch (type) {
        // От УВМ к СВ-М
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
        // От СВ-М к УВМ
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

     //qDebug() << "Raw IPC line:" << line;

     QStringList parts = line.split(';');
     if (parts.isEmpty()) {
         qWarning() << "Empty line after split:" << line;
         return;
     }

     QString eventDirectionOrType = parts[0].trimmed(); // SENT, RECV, EVENT
     int svmId = -1;
     int msgType = -1;
     QString msgName = "N/A";
     int msgNum = -1;
     int lak = -1; // 0xFF как "не применимо" или "не известно"
     QString details = "";
     QDateTime timestamp = QDateTime::currentDateTime();

     // Создаем QMap для удобного парсинга ключ-значение
     QMap<QString, QString> fieldsMap;
     for (int i = 1; i < parts.size(); ++i) { // Начинаем с 1, т.к. 0 - это тип строки
         QStringList key_value = parts[i].split(':');
         if (key_value.size() == 2) {
             fieldsMap.insert(key_value[0].trimmed(), key_value[1].trimmed());
         } else if (!parts[i].trimmed().isEmpty()){ // Игнорируем пустые части после последнего '|'
             qWarning() << "Malformed field in IPC line:" << parts[i] << "Full line:" << line;
         }
     }

     bool ok;
     // Обязательное поле SVM_ID
     if (fieldsMap.contains("SVM_ID")) {
         svmId = fieldsMap.value("SVM_ID").toInt(&ok);
         if (!ok) { qWarning() << "Failed to parse SVM_ID:" << fieldsMap.value("SVM_ID"); return; }
     } else {
         qWarning() << "SVM_ID field missing in IPC line:" << line; return;
     }

     // Поля для SENT и RECV
     if (eventDirectionOrType == "SENT" || eventDirectionOrType == "RECV") {
         if (fieldsMap.contains("Type")) {
             msgType = fieldsMap.value("Type").toInt(&ok);
             if (ok) msgName = getMessageNameByType(msgType);
             else qWarning() << "Failed to parse Type for SVM" << svmId << ":" << fieldsMap.value("Type");
         }
         if (fieldsMap.contains("Num")) {
             msgNum = fieldsMap.value("Num").toInt(&ok);
             if (!ok) qWarning() << "Failed to parse Num for SVM" << svmId << ":" << fieldsMap.value("Num");
         }
         if (fieldsMap.contains("LAK")) {
             // strtol используется для поддержки 0x префикса
             lak = fieldsMap.value("LAK").toUInt(&ok, 0);
             if (!ok) qWarning() << "Failed to parse LAK for SVM" << svmId << ":" << fieldsMap.value("LAK");
         }
         // Детали для SENT/RECV пока не передаем, можно добавить PayloadInfo
     }
     // Поля для EVENT
     else if (eventDirectionOrType == "EVENT") {
         if (fieldsMap.contains("Type")) { // "Type" для события - это его имя
             msgName = fieldsMap.value("Type"); // Имя события уже в msgName
         }
         if (fieldsMap.contains("Details")) {
             details = fieldsMap.value("Details");
         }

         // Если это событие изменения статуса линка, испускаем отдельный сигнал
         if(msgName == "LinkStatus") {
             QStringList status_part = details.split('=');
             if (status_part.size() == 2 && status_part[0].trimmed() == "NewStatus") {
                 int newStatus = status_part[1].trimmed().toInt(&ok);
                 if (ok) emit svmLinkStatusChanged(svmId, newStatus);
                 else qWarning() << "Failed to parse NewStatus for LinkStatus event:" << details;
             }
         }
     } else {
         qWarning() << "Unknown IPC message main type:" << eventDirectionOrType << "Full line:" << line;
         return; // Неизвестный тип строки IPC
     }

     emit newMessageOrEvent(svmId, timestamp, eventDirectionOrType, msgType, msgName, msgNum, lak, details);
}