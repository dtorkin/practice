#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>
#include <QDateTime>

#include "uvmmonitorclient.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QLabel;
class QTableWidget;
class QPushButton;

// Для хранения предыдущих значений, чтобы не дублировать события SENT/RECV
// Это уже не нужно, если uvm_app отправляет только новые события
// struct LastMessageInfo {
//     int type = -1;
//     int num = -1;
// };

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
                             int assignedLak, quint32 bcb, const QString& details); // <-- ДОБАВЛЕН quint32 bcb


    void updateConnectionStatus(bool connected, const QString& message);
    void updateSvmLinkStatusDisplay(int svmId, int newStatus, int assignedLak);

    void onSaveLogAllClicked();


private:
    Ui::MainWindow *ui;
    UvmMonitorClient *m_client;

    QVector<QLabel*> m_statusLabels;
    QVector<QLabel*> m_lakLabels;
    QVector<QLabel*> m_bcbLabels;
    QVector<QTableWidget*> m_logTables;
    QVector<QLabel*> m_errorDisplays;

    QVector<int> m_assignedLaks; // Храним назначенные LAK
	
	QVector<quint32> m_lastDisplayedBcb; // Для отслеживания последнего отображенного BCB

    QString statusToString(int status);
    QString statusToStyleSheet(int status);
    void saveTableLogToFile(int svmId, const QString& baseDir);
    void initTableWidget(QTableWidget* table);
};
#endif // MAINWINDOW_H