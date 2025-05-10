#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QDateTime>

#include "uvmmonitorclient.h" // Для сигналов

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QLabel;
class QTableWidget;
class QPushButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onNewMessageOrEvent(int svmId, const QDateTime& timestamp,
                             const QString& directionOrEventType,
                             int msgType, const QString& msgName, int msgNum,
                             int assignedLak, const QString& details);

    void updateConnectionStatus(bool connected, const QString& message);
    void updateSvmLinkStatusDisplay(int svmId, int newStatus);

    void onSaveLogAllClicked();


private:
    Ui::MainWindow *ui;
    UvmMonitorClient *m_client;

    QVector<QLabel*> m_statusLabels;
    QVector<QLabel*> m_lakLabels;
    QVector<QLabel*> m_bcbLabels;
    QVector<QTableWidget*> m_logTables;
    QVector<QLabel*> m_errorDisplays;

    // Для отслеживания последних номеров BCB, чтобы не дублировать его вывод
    QVector<quint32> m_lastDisplayedBcb;


    QString statusToString(int status);
    QString statusToStyleSheet(int status);
    void saveTableLogToFile(int svmId, const QString& baseDir); // Добавлен аргумент директории
    void initTableWidget(QTableWidget* table); // Вспомогательная функция для настройки таблицы
};
#endif // MAINWINDOW_H