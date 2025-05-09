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
		m_buffer.remove(0, newlinePos + 1);		// Удаляем строку и \n из буфера

		parseData(line); // Парсим полученную строку
	}
}

void UvmMonitorClient::parseData(const QByteArray& data)
{
     QString line = QString::fromUtf8(data).trimmed();
     // qDebug() << "GUI Client Parsing line:" << line;

     QStringList svm_records = line.split('|', Qt::SkipEmptyParts);

     for (const QString& record : svm_records) {
         SvmStatusData statusData; // Инициализируем значениями по умолчанию
         statusData.id = -1;
         statusData.status = 0; // UVM_LINK_INACTIVE
         statusData.lak = 0;
         statusData.lastSentType = -1;
         statusData.lastSentNum = -1;
         statusData.lastRecvType = -1;
         statusData.lastRecvNum = -1;
         statusData.bcb = 0;
         statusData.rsk = 0xFF;
         statusData.warnTKS = 0;
         statusData.timeoutDetected = false;
         statusData.lakFailDetected = false;
         statusData.ctrlFailDetected = false;
         statusData.simDisconnect = false;
         statusData.discCountdown = -1;


         QStringList fields = record.split(';');

         for (const QString& field : fields) {
             QStringList key_value = field.split(':');
             if (key_value.size() == 2) {
                 QString key = key_value[0].trimmed();
                 QString value = key_value[1].trimmed();
                 bool ok;

                 if (key == "ID") {
                     statusData.id = value.toInt(&ok);
                     if (!ok) qWarning() << "Failed to parse ID:" << value << "in record:" << record;
                 } else if (key == "Status") {
                     statusData.status = value.toInt(&ok);
                     if (!ok) qWarning() << "Failed to parse Status:" << value;
                 } else if (key == "LAK") {
                     statusData.lak = value.toInt(&ok); // LAK приходит как десятичное
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
                 } else if (key == "BCB") {
                     statusData.bcb = value.toUInt(&ok); // uint32_t
                     if (!ok) qWarning() << "Failed to parse BCB:" << value;
                 }
                 // *************** ДОБАВЛЕН ПАРСИНГ НОВЫХ ПОЛЕЙ ***************
                 else if (key == "RSK") {
                     statusData.rsk = static_cast<quint8>(value.toUShort(&ok)); // uint8_t
                     if (!ok) qWarning() << "Failed to parse RSK:" << value;
                 } else if (key == "WarnTKS") {
                     statusData.warnTKS = static_cast<quint8>(value.toUShort(&ok)); // uint8_t
                     if (!ok) qWarning() << "Failed to parse WarnTKS:" << value;
                 } else if (key == "Timeout") {
                     statusData.timeoutDetected = (value.toInt(&ok) == 1);
                     if (!ok) qWarning() << "Failed to parse Timeout:" << value;
                 } else if (key == "LAKFail") {
                     statusData.lakFailDetected = (value.toInt(&ok) == 1);
                     if (!ok) qWarning() << "Failed to parse LAKFail:" << value;
                 } else if (key == "CtrlFail") {
                     statusData.ctrlFailDetected = (value.toInt(&ok) == 1);
                     if (!ok) qWarning() << "Failed to parse CtrlFail:" << value;
                 } else if (key == "SimDisc") {
                     statusData.simDisconnect = (value.toInt(&ok) == 1);
                     if (!ok) qWarning() << "Failed to parse SimDisc:" << value;
                 } else if (key == "DiscCnt") {
                     statusData.discCountdown = value.toInt(&ok);
                     if (!ok) qWarning() << "Failed to parse DiscCnt:" << value;
                 }
                 // *************************************************************
                 else {
                     // qWarning() << "Unknown key in IPC data:" << key << "from record:" << record;
                 }
             } else if (!field.isEmpty()) { // Логируем только непустые некорректные поля
                  qWarning() << "Invalid field format in IPC data:" << field << "from record:" << record;
             }
         }

         if (statusData.id != -1) {
             emit svmStatusUpdated(statusData);
         } else if (!record.isEmpty()){ // Логируем только если запись не пустая
             qWarning() << "Parsed record without valid ID in IPC data:" << record;
         }
     }
}