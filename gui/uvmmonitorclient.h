#ifndef UVMMONITORCLIENT_H
#define UVMMONITORCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QString> 
#include <QDateTime>

// Структура для хранения распарсенных данных о состоянии одного SVM
struct SvmStatusData {
    int id = -1;                // ID экземпляра SVM
    int status = 0;             // UvmLinkStatus (как int)
    int lak = 0;                // Логический адрес
    int lastSentType = -1;      // Тип последнего отправленного сообщения
    int lastSentNum = -1;       // Номер последнего отправленного сообщения
    int lastRecvType = -1;      // Тип последнего полученного сообщения
    int lastRecvNum = -1;       // Номер последнего полученного сообщения
    quint32 bcb = 0;            // Последний BCB

    // --- Поля для ошибок и статусов сбоев ---
    quint8 rsk = 0xFF;          // Последний RSK (0xFF = не было)
    quint8 warnTKS = 0;         // Последний TKS (0 = не было)
    bool timeoutDetected = false;
    bool lakFailDetected = false;
    bool ctrlFailDetected = false;
    bool simDisconnect = false;   // SVM имитирует дисконнект
    int discCountdown = -1;     // Счетчик до дисконнекта SVM (-1 = выкл)

    // Можно добавить time_t last_activity_time, если нужно отображать
};


struct SvmLinkOverallStatus {
    int id = -1;
    int statusCode = 0; // Соответствует UvmLinkStatus
    int lak = 0;
};

class UvmMonitorClient : public QObject
{
    Q_OBJECT
public:
    explicit UvmMonitorClient(QObject *parent = nullptr);
    ~UvmMonitorClient();

    void connectToServer(const QString& host = "127.0.0.1", quint16 port = 12345);
    void disconnectFromServer();

signals:
    // Сигнал для нового сообщения или события
    void newMessageOrEvent(int svmId, const QDateTime& timestamp,
                           const QString& directionOrEventType, // "SENT", "RECV", "EVENT"
                           int msgType,        // Тип сообщения (если SENT/RECV)
                           const QString& msgName,    // Имя сообщения (если SENT/RECV)
                           int msgNum,         // Номер сообщения (если SENT/RECV)
                           int assignedLak,    // LAK, которому адресовано (SENT) или от которого пришло (RECV)
                           const QString& details);   // Дополнительные детали (для EVENT)

    // Сигнал об изменении статуса подключения к uvm_app
    void connectionStatusChanged(bool connected, const QString& message);
    // Сигнал для обновления общего статуса SVM (например, ACTIVE/FAILED)
    void svmLinkStatusChanged(int svmId, int newStatus, int assignedLak);


private slots:
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError socketError);
    void onReadyRead();
    void attemptConnection();

private:
    QTcpSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QString m_host;
    quint16 m_port;
    QByteArray m_buffer;

    void parseData(const QByteArray& data);
    QString getMessageNameByType(int type); // Вспомогательная функция
};

#endif // UVMMONITORCLIENT_H