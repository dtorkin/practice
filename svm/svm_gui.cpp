#include "svm_gui.h"
#include <QWidget>
#include <QVBoxLayout> // Вертикальный компоновщик
#include <QHBoxLayout> // Горизонтальный компоновщик
#include <QGroupBox>   // Рамка с заголовком для панели
#include <QString>
#include <QThread>     // Для sleep (если нужно)
#include <QDebug>      // Для отладочного вывода

// Для доступа к функциям pthread_mutex
#include <pthread.h>

// Определяем глобальные переменные из C кода как extern "C"
extern "C" {
    // Повторяем объявления extern, чтобы линкер их нашел
    SvmInstance svm_instances[MAX_SVM_INSTANCES];
    pthread_mutex_t svm_instances_mutex;
    // extern pthread_mutex_t svm_counters_mutex; // Если используется
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
        mainLayout->addWidget(panel, i / 2, i % 2); // Размещение 2x2
    }

    setCentralWidget(centralWidget);
    setWindowTitle("SVM Monitor");
    resize(600, 400); // Начальный размер окна

    // Создаем и запускаем таймер для обновления раз в секунду
    updateTimer = new QTimer(this);
    connect(updateTimer, &QTimer::timeout, this, &SvmMainWindow::updateDisplay);
    updateTimer->start(1000); // Интервал 1000 мс = 1 сек
}

// Деструктор
SvmMainWindow::~SvmMainWindow() {
    // Таймер удалится автоматически вместе с окном
}

// Функция создания панели для одного SVM
QWidget* SvmMainWindow::createSvmPanel(int svmId) {
    QGroupBox *groupBox = new QGroupBox(QString("SVM Instance %1").arg(svmId));
    QVBoxLayout *panelLayout = new QVBoxLayout(groupBox);

    // Создаем QLabel'ы и сохраняем указатели на них в массивах
    lakLabels[svmId] = new QLabel("LAK: ---");
    statusLabels[svmId] = new QLabel("Status: INACTIVE");
    bcbLabels[svmId] = new QLabel("BCB: 0");
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
        LogicalAddress lak = 0;
        uint32_t bcb = 0;
        // uint16_t kla = 0, ksa = 0;
        // uint32_t sla = 0;

        // Блокируем мьютекс экземпляра перед чтением его данных
        // Важно: НЕЛЬЗЯ долго держать мьютекс в потоке GUI
        // Если чтение занимает время, лучше использовать отдельный поток для чтения
        if (pthread_mutex_trylock(&svm_instances[i].instance_mutex) == 0) { // Пытаемся захватить без блокировки
            isActive = svm_instances[i].is_active;
            lak = svm_instances[i].assigned_lak;
            bcb = svm_instances[i].bcb_counter; // Читаем volatile счетчик
            // kla = svm_instances[i].link_up_changes_counter;
            // sla = svm_instances[i].link_up_low_time_us100;
            // ksa = svm_instances[i].sign_det_changes_counter;
            pthread_mutex_unlock(&svm_instances[i].instance_mutex); // Отпускаем
        } else {
             // Не удалось захватить мьютекс (занят другим потоком)
             // Пропускаем обновление для этого SVM в этот раз или показываем старые данные
             // qWarning() << "Could not lock mutex for SVM" << i << "to update GUI";
             continue; // Пропустить обновление
        }
        // --- Конец чтения данных ---

        // Обновляем QLabel'ы
        lakLabels[i]->setText(QString("LAK: 0x%1").arg(lak, 2, 16, QChar('0')));
        statusLabels[i]->setText(QString("Status: %1").arg(isActive ? "ACTIVE" : "INACTIVE"));
        bcbLabels[i]->setText(QString("BCB: %1 (0x%2)").arg(bcb).arg(bcb, 8, 16, QChar('0')));
        // klaLabels[i]->setText(QString("KLA: %1").arg(kla));
        // slaLabels[i]->setText(QString("SLA: %1").arg(sla));
        // ksaLabels[i]->setText(QString("KSA: %1").arg(ksa));

        // Можно добавить изменение цвета фона панели в зависимости от статуса
        if (isActive) {
            statusLabels[i]->setStyleSheet("QLabel { color : green; }");
        } else {
             statusLabels[i]->setStyleSheet("QLabel { color : red; }");
        }
    }
}