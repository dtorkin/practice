/*
 * svm/svm_gui.cpp
 * Описание: Реализация главного окна GUI для мониторинга SVM
 */
#include "svm_gui.h"
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout> // Используем QGridLayout
#include <QGroupBox>
#include <QString>
#include <QThread>
#include <QDebug>
#include <QMutexLocker> // Более удобный способ блокировки QMutex

// Для доступа к функциям pthread_mutex
#include <pthread.h>
#include <time.h> // Для time()

// Определяем глобальные переменные из C кода как extern "C"
// Это нужно, чтобы C++ линкер нашел их определения в C объектных файлах
extern "C" {
    SvmInstance svm_instances[MAX_SVM_INSTANCES];
    // Нам нужен только мьютекс каждого экземпляра для чтения его состояния
    // pthread_mutex_t svm_instances_mutex; // Этот мьютекс для изменения массива не нужен в GUI
}

// Конструктор окна
SvmMainWindow::SvmMainWindow(QWidget *parent) : QMainWindow(parent) {
    // Создаем центральный виджет и главный компоновщик (сетка 2x2)
    QWidget *centralWidget = new QWidget(this);
    QGridLayout *mainLayout = new QGridLayout(centralWidget);

    // Создаем 4 панели для SVM
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        QWidget *panel = createSvmPanel(i);
        // Размещаем панель в сетке
        mainLayout->addWidget(panel, i / 2, i % 2); // Размещение 2x2 (0,0), (0,1), (1,0), (1,1)
    }

    setCentralWidget(centralWidget);
    setWindowTitle("SVM Monitor");
    resize(600, 400); // Начальный размер окна

    // Создаем и запускаем таймер для обновления раз в секунду
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &SvmMainWindow::updateDisplay);
    updateTimer->start(1000); // Интервал 1000 мс = 1 сек

    qDebug() << "SVM GUI Main Window Created."; // Отладочное сообщение Qt
}

// Деструктор
SvmMainWindow::~SvmMainWindow() {
    qDebug() << "SVM GUI Main Window Destroyed.";
    // Таймер удалится автоматически, так как this - его родитель
}

// Функция создания панели для одного SVM
QWidget* SvmMainWindow::createSvmPanel(int svmId) {
    QGroupBox *groupBox = new QGroupBox(QString("SVM Instance %1").arg(svmId));
    groupBox->setMinimumWidth(250); // Минимальная ширина панели
    QVBoxLayout *panelLayout = new QVBoxLayout(groupBox);

    // Создаем QLabel'ы и сохраняем указатели на них в массивах
    lakLabels[svmId] = new QLabel("LAK: ---");
    statusLabels[svmId] = new QLabel("Status: INACTIVE");
    bcbLabels[svmId] = new QLabel("BCB: 0 (0x00000000)");
    // klaLabels[svmId] = new QLabel("KLA: 0");
    // slaLabels[svmId] = new QLabel("SLA: 0");
    // ksaLabels[svmId] = new QLabel("KSA: 0");

    // Добавляем QLabel'ы на панель
    panelLayout->addWidget(lakLabels[svmId]);
    panelLayout->addWidget(statusLabels[svmId]);
    panelLayout->addWidget(bcbLabels[svmId]);
    // panelLayout->addWidget(klaLabels[svmId]);
    // panelLayout->addWidget(slaLabels[svmId]);
    // panelLayout->addWidget(ksaLabels[svmId]);

    panelLayout->addStretch(); // Чтобы элементы прижимались к верху

    return groupBox;
}

// Слот обновления данных на экране
void SvmMainWindow::updateDisplay() {
    // Проходим по всем экземплярам SVM
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        // --- Читаем данные из SvmInstance БЕЗОПАСНО ---
        bool isActive = false;
        LogicalAddress lak = (LogicalAddress)0; // Инициализация с приведением типа
        uint32_t bcb = 0;
        // uint16_t kla = 0, ksa = 0;
        // uint32_t sla = 0;
        bool data_read_ok = false; // Флаг, удалось ли прочитать данные

        // Пытаемся захватить мьютекс экземпляра без блокировки
        if (pthread_mutex_trylock(&svm_instances[i].instance_mutex) == 0) {
            // Захватили мьютекс - читаем данные
            isActive = svm_instances[i].is_active;
            lak = svm_instances[i].assigned_lak;
            bcb = svm_instances[i].bcb_counter; // Читаем volatile счетчик
            // kla = svm_instances[i].link_up_changes_counter;
            // sla = svm_instances[i].link_up_low_time_us100;
            // ksa = svm_instances[i].sign_det_changes_counter;
            pthread_mutex_unlock(&svm_instances[i].instance_mutex); // Отпускаем
            data_read_ok = true;
        } else {
             // Не удалось захватить мьютекс - пропускаем обновление в этот раз
             // Это нормально, если другой поток держит мьютекс недолго
             // qWarning() << "Could not lock mutex for SVM" << i << "to update GUI";
             data_read_ok = false;
        }
        // --- Конец чтения данных ---

        // Обновляем QLabel'ы только если удалось прочитать данные
        if (data_read_ok) {
            lakLabels[i]->setText(QString("LAK: 0x%1").arg(lak, 2, 16, QChar('0').toUpper()));
            statusLabels[i]->setText(QString("Status: %1").arg(isActive ? "ACTIVE" : "INACTIVE"));
            bcbLabels[i]->setText(QString("BCB: %1 (0x%2)").arg(bcb).arg(bcb, 8, 16, QChar('0').toUpper()));
            // klaLabels[i]->setText(QString("KLA: %1").arg(kla));
            // slaLabels[i]->setText(QString("SLA: %1").arg(sla));
            // ksaLabels[i]->setText(QString("KSA: %1").arg(ksa));

            // Меняем цвет статуса
            if (isActive) {
                statusLabels[i]->setStyleSheet("QLabel { color : green; font-weight: bold; }");
            } else {
                 statusLabels[i]->setStyleSheet("QLabel { color : red; }");
            }
        }
    }
}