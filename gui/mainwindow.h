#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QMap> // Для хранения предыдущих номеров сообщений

#include "uvmmonitorclient.h" // Для структуры SvmStatusData

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QLabel;
class QListWidget; // Предварительное объявление

// Максимальное количество элементов в истории для каждого SVM
const int MAX_HISTORY_ITEMS = 20;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void updateSvmDisplay(const SvmStatusData& data);
    void updateConnectionStatus(bool connected, const QString& message);

private:
    Ui::MainWindow *ui;
    UvmMonitorClient *m_client;

    // Массивы для быстрого доступа к виджетам по ID SVM
    QVector<QLabel*> m_statusLabels;
    QVector<QLabel*> m_lakLabels;
    QVector<QLabel*> m_bcbLabels;
    QVector<QListWidget*> m_historyWidgets; // Для истории сообщений
    QVector<QLabel*> m_errorLabels;      // Для отображения ошибок

    // Хранение предыдущих номеров сообщений для определения новых
    // Используем QMap, чтобы не зависеть от MAX_SVM_CONFIGS и обрабатывать только существующие ID
    QMap<int, int> m_prevSentNum; // key: svm_id, value: last_sent_msg_num
    QMap<int, int> m_prevRecvNum; // key: svm_id, value: last_recv_msg_num


    QString statusToString(int status);
    QString statusToStyleSheet(int status);
    // Вспомогательная функция для получения имени типа сообщения (если есть)
    QString messageTypeToName(int type);
};
#endif // MAINWINDOW_H