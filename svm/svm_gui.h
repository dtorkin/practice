#ifndef SVM_GUI_H
#define SVM_GUI_H

#include <QMainWindow> // Базовый класс для окон
#include <QLabel>      // Для отображения текста
#include <QTimer>      // Для периодического обновления
#include <QMutex>      // Для блокировки мьютексов Pthreads из Qt

// Подключаем наши C-шные определения
extern "C" {
    #include "../config/config.h" // Нужен для MAX_SVM_INSTANCES и AppConfig (если нужен доступ)
    #include "svm_types.h"       // Нужен для SvmInstance
    // Подключаем для доступа к глобальным переменным SVM
    extern SvmInstance svm_instances[MAX_SVM_INSTANCES];
    extern pthread_mutex_t svm_instances_mutex; // Общий мьютекс массива (если он есть)
    // extern pthread_mutex_t svm_counters_mutex; // Если счетчики защищены общим мьютексом
}

// Класс нашего главного окна
class SvmMainWindow : public QMainWindow {
    Q_OBJECT // Обязательный макрос для использования сигналов/слотов Qt

public:
    // Конструктор окна
    explicit SvmMainWindow(QWidget *parent = nullptr);
    // Деструктор
    ~SvmMainWindow();

private slots:
    // Слот, который будет вызываться по таймеру для обновления данных
    void updateDisplay();

private:
    // Функция для создания панели для одного SVM
    QWidget* createSvmPanel(int svmId);

    // Массив указателей на QLabel для отображения данных каждого SVM
    QLabel* statusLabels[MAX_SVM_INSTANCES];
    QLabel* lakLabels[MAX_SVM_INSTANCES];
    QLabel* bcbLabels[MAX_SVM_INSTANCES];
    // Добавь другие QLabel для нужных счетчиков...
    // QLabel* klaLabels[MAX_SVM_INSTANCES];
    // QLabel* slaLabels[MAX_SVM_INSTANCES];
    // QLabel* ksaLabels[MAX_SVM_INSTANCES];

    QTimer *updateTimer; // Таймер для обновления

    // Мьютекс для безопасного доступа к данным SVM из потока GUI
    // Используем QMutex, который может оборачивать pthread_mutex
    // QMutex svmDataMutex; // Вариант 1: Обертка Qt
};

#endif // SVM_GUI_H