/*
 * svm/svm_timers.h
 *
 * Описание:
 * Объявления для управления таймерами и счетчиками SVM.
 */

#ifndef SVM_TIMERS_H
#define SVM_TIMERS_H

#include <stdint.h>
#include <time.h>
#include <limits.h>  // Для UINT*_MAX констант
#include <pthread.h> // Для pthread_mutex_t, pthread_cond_t
#include <stdbool.h> // Для bool

// --- Константы для таймеров и вывода ---
#define TIMER_INTERVAL_BCB_MS 50           // Интервал основного таймера BCB (в миллисекундах)
#define TIMER_INTERVAL_LINK_STATUS_MS 2000 // Частота эмуляции проверки статуса линии (в миллисекундах)
#define LINK_CHANGE_PROBABILITY 2          // Вероятность изменения LinkUp (1/X)
#define LINK_LOW_PROBABILITY 10            // Вероятность увеличения времени низкого уровня (1/Y) когда LinkUp меняется
#define SIGN_DET_CHANGE_PROBABILITY 3      // Вероятность изменения SignDet (1/Z)
#define COUNTER_PRINT_INTERVAL_SEC 5       // Интервал вывода счетчиков в консоль (в секундах)

// --- Внешние переменные счетчиков (определены в svm_timers.c) ---
extern volatile uint32_t bcbCounter;          // Счетчик времени работы СВ-М (инкремент каждые TIMER_INTERVAL_BCB_MS)
extern uint16_t linkUpChangesCounter;         // Счетчик изменений LinkUp (1->0)
extern uint32_t linkUpLowTimeSeconds;         // Интегральное время низкого уровня LinkUp (в единицах 1/100 мкс)
extern uint16_t signDetChangesCounter;        // Счетчик изменений SignDet (1->0)

// --- Мьютекс и условная переменная для таймера и счетчиков (определены в svm_timers.c) ---
extern pthread_mutex_t svm_counters_mutex;    // Мьютекс для защиты ВСЕХ счетчиков выше
extern pthread_cond_t svm_timer_cond;         // Условная переменная для пробуждения потока таймера
extern volatile bool timer_thread_running;    // Флаг для управления жизненным циклом потока таймера

// --- Функции ---

/**
 * @brief Инициализирует мьютекс и условную переменную для счетчиков/таймера.
 * Также инициализирует генератор случайных чисел.
 * @return 0 при успехе, -1 при ошибке.
 */
int init_svm_counters_mutex_and_cond(void);

/**
 * @brief Уничтожает мьютекс и условную переменную счетчиков/таймера.
 */
void destroy_svm_counters_mutex_and_cond(void);

/**
 * @brief Функция потока таймера SVM.
 * Периодически обновляет счетчики BCB, KLA, SLA, KSA.
 * Защищает доступ к счетчикам с помощью мьютекса.
 * Ожидает с использованием pthread_cond_timedwait.
 * @param arg Не используется (должен быть NULL).
 * @return NULL.
 */
void* timer_thread_func(void* arg);


/**
 * @brief Устанавливает флаг завершения и сигнализирует потоку таймера.
 * Эта функция потокобезопасна.
 */
void stop_timer_thread(void);


/**
 * @brief Выводит текущие значения счетчиков SVM в консоль (потокобезопасно).
 */
void print_counters(void);


/**
 * @brief Получает текущее значение счетчика BCB (потокобезопасно).
 * @return Текущее значение BCB.
 */
uint32_t get_bcb_counter(void);

/**
 * @brief Получает текущие значения счетчиков состояния линии (потокобезопасно).
 * @param kla Указатель для записи значения KLA (может быть NULL).
 * @param sla Указатель для записи значения SLA (может быть NULL).
 * @param ksa Указатель для записи значения KSA (может быть NULL).
 */
void get_line_status_counters(uint16_t *kla, uint32_t *sla, uint16_t *ksa);


#endif // SVM_TIMERS_H