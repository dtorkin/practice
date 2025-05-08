#include "uvmmonitorclient.h"
#include <QDebug>
#include <QStringList>
#include <QThread> // Для sleep

UvmMonitorClient::UvmMonitorClient(QObject *parent)
    : QObject{parent}
{
    m_socket = new QTcpSocket(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(5000); // Пытаться переподключиться каждые 5 секунд

    connect(m_socket, &QTcpSocket::connected, this, &UvmMonitorClient::onConnected);
    // Для Qt6 используйте: connect(m_socket, &QTcpSocket::errorOccurred, this, &UvmMonitorClient::onErrorOccurred);
    // Для Qt5 используйте старый синтаксис со SIGNAL/SLOT или QOverload
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred), this, &UvmMonitorClient::onErrorOccurred);
    connect(m_socket, &QTcpSocket::disconnected, this, &UvmMonitorClient::onDisconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &UvmMonitorClient::onReadyRead);
    connect(m_reconnectTimer, &QTimer::timeout, this, &UvmMonitorClient::attemptConnection);
}

UvmMonitorClient::~UvmMonitorClient()
{
    // m_socket удалится автоматически, т.к. parent = this
    // m_reconnectTimer удалится автоматически
}

void UvmMonitorClient::connectToServer(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    qDebug() << "Attempting to connect to UVM server at" << m_host << ":" << m_port;
    m_reconnectTimer->stop(); // Остановить таймер перед новой попыткой
    if (m_socket->state() == QAbstractSocket::UnconnectedState) {
         m_buffer.clear(); // Очистить буфер при новом подключении
        m_socket->connectToHost(m_host, m_port);
    } else {
        qDebug() << "Socket is not in UnconnectedState:" << m_socket->state();
        // Возможно, уже подключен или в процессе
        if(m_socket->state() == QAbstractSocket::ConnectedState) {
            emit connectionStatusChanged(true, "Already connected.");
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
    emit connectionStatusChanged(false, "Disconnected. Retrying...");
    m_reconnectTimer->start(); // Запустить таймер для переподключения
}

void UvmMonitorClient::onErrorOccurred(QAbstractSocket::SocketError socketError)
{
    qWarning() << "Socket error:" << socketError << m_socket->errorString();
    emit connectionStatusChanged(false, "Error: " + m_socket->errorString());
    // Таймер переподключения запустится после сигнала disconnected, который обычно следует за ошибкой
     if (m_socket->state() == QAbstractSocket::UnconnectedState && !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start(); // На всякий случай, если disconnected не пришел
     }
}

void UvmMonitorClient::attemptConnection()
{
     qDebug() << "Attempting to reconnect...";
     connectToServer(m_host, m_port); // Используем сохраненные хост и порт
}


void UvmMonitorClient::onReadyRead()
{
    m_buffer.append(m_socket->readAll());

    // Пытаемся обработать данные в буфере, пока есть полные строки (\n)
    while (m_buffer.contains('\n')) {
        int newlinePos = m_buffer.indexOf('\n');
        QByteArray line = m_buffer.left(newlinePos); // Берем данные до \n
        m_buffer.remove(0, newlinePos + 1);          // Удаляем строку и \n из буфера

        parseData(line); // Парсим полученную строку
    }
}

void UvmMonitorClient::parseData(const QByteArray& data)
{
     QString line = QString::fromUtf8(data).trimmed(); // Убираем пробельные символы по краям
     //qDebug() << "Parsing line:" << line;

     QStringList svm_records = line.split('|', Qt::SkipEmptyParts); // Разделяем по '|'

     for (const QString& record : svm_records) {
         SvmStatusData statusData;
         QStringList fields = record.split(';'); // Разделяем по ';'

         for (const QString& field : fields) {
             QStringList key_value = field.split(':');
             if (key_value.size() == 2) {
                 QString key = key_value[0].trimmed();
                 QString value = key_value[1].trimmed();
                 bool ok;
                 if (key == "ID") {
                     statusData.id = value.toInt(&ok);
                     if (!ok) qWarning() << "Failed to parse ID:" << value;
                 } else if (key == "Status") {
                     statusData.status = value.toInt(&ok);
                     if (!ok) qWarning() << "Failed to parse Status:" << value;
                 } else if (key == "LAK") {
                      statusData.lak = value.toInt(&ok); // LAK приходит как десятичное число
                     if (!ok) qWarning() << "Failed to parse LAK:" << value;
                 } else if (key == "SentType") {
                      statusData.lastSentType = value.toInt(&ok);
                     if (!ok) qWarning() << "Failed to parse SentType:" << value;
                 } else if (key == "SentNum") {
                      statusData.lastSentNum = value.toInt(&ok);
                     if (!ok) qWarning() << "Failed to parse SentNum:" << value;
                 } else if (key == "RecvType") {
                      statusData.lastRecvType = value.toInt(&ok);
                     if (!ok) qWarning() << "Failed to parse RecvType:" << value;
                 } else if (key == "RecvNum") {
                      statusData.lastRecvNum = value.toInt(&ok);
                     if (!ok) qWarning() << "Failed to parse RecvNum:" << value;
                 }
                 // Добавить парсинг других полей (Time и т.д.)
             } else {
                  qWarning() << "Invalid field format:" << field;
             }
         }

         // Испускаем сигнал, если ID валидный
         if (statusData.id != -1) {
              //qDebug() << "Emitting status for ID:" << statusData.id;
             emit svmStatusUpdated(statusData);
         } else {
             qWarning() << "Parsed record without valid ID:" << record;
         }
     }
}