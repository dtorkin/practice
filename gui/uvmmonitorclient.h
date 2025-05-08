#ifndef UVMMONITORCLIENT_H
#define UVMMONITORCLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QTimer>

// Для передачи статуса используем структуры, но можно и просто аргументы в сигнале
struct SvmStatusData {
    int id = -1;
    int status = 0; // Соответствует UvmLinkStatus
    int lak = 0;
    int lastSentType = -1;
    int lastSentNum = -1;
    int lastRecvType = -1;
    int lastRecvNum = -1;
    // Добавить другие поля при необходимости
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
    // Сигнал испускается для каждого SVM при получении обновления
    void svmStatusUpdated(const SvmStatusData& data);
    // Сигнал об изменении статуса подключения к uvm_app
    void connectionStatusChanged(bool connected, const QString& message);


private slots:
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError socketError);
    void onReadyRead();
    void attemptConnection(); // Слот для таймера переподключения

private:
    QTcpSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr; // Таймер для попыток переподключения
    QString m_host;
    quint16 m_port;
    QByteArray m_buffer; // Буфер для накопления данных

    void parseData(const QByteArray& data); // Парсер для входящих данных
};

#endif // UVMMONITORCLIENT_H