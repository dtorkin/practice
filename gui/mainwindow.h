#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector> // Для хранения указателей на QLabel

#include "uvmmonitorclient.h" // Для структуры SvmStatusData

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QLabel; // Предварительное объявление

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // Слот для обновления данных одного SVM
    void updateSvmDisplay(const SvmStatusData& data);
    // Слот для отображения статуса подключения к uvm_app
    void updateConnectionStatus(bool connected, const QString& message);


private:
    Ui::MainWindow *ui;
    UvmMonitorClient *m_client;

    // Массивы для быстрого доступа к виджетам по ID SVM
    QVector<QLabel*> m_statusLabels;
    QVector<QLabel*> m_lakLabels;
    QVector<QLabel*> m_lastSentLabels;
    QVector<QLabel*> m_lastRecvLabels;
	QVector<QLabel*> m_bcbLabels;

    QString statusToString(int status); // Вспомогательная функция
    QString statusToStyleSheet(int status); // Вспомогательная функция
};
#endif // MAINWINDOW_H