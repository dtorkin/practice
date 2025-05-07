/*
 * svm/svm_timers.h
 *
 * Описание:
 * Объявления для управления таймером и счетчиками для МНОЖЕСТВА экземпляров SVM.
 */

#ifndef SVM_TIMERS_H
#define SVM_TIMERS_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include "svm_types.h" // <-- ВКЛЮЧЕНО для SvmInstance

// --- Константы для таймеров ---
#define TIMER_INTERVAL_BCB_MS 50           // Интервал основного таймера BCB (в миллисекундах)
#define TIMER_INTERVAL_LINK_STATUS_MS 2000 // Частота эмуляции проверки статуса линии (в миллисекундах)
#define LINK_CHANGE_PROBABILITY 2          // Вероятность изменения LinkUp (1/X)
#define LINK_LOW_PROBABILITY 10            // Вероятность увеличения времени низкого уровня (1/Y) когда LinkUp меняется
#define SIGN_DET_CHANGE_PROBABILITY 3      // Вероятность изменения SignDet (1/Z)

// --- Глобальные переменные для таймера ---
// (Определены в svm_timers.c)
extern pthread_mutex_t svm_timer_management_mutex; // Мьютекс для управления запуском/остановкой таймера
extern pthread_cond_t svm_timer_cond;              // Условная переменная для пробуждения/остановки таймера
extern volatile bool global_timer_keep_running;    // Глобальный флаг для основного цикла таймера

/**
 * @brief Инициализирует глобальный мьютекс и условную переменную для таймера.
 * Также инициализирует генератор случайных чисел.
 * @return 0 при успехе, -1 при ошибке.
 */
int init_svm_timer_sync(void);

/**
 * @brief Уничтожает глобальный мьютекс и условную переменную таймера.
 */
void destroy_svm_timer_sync(void);

/**
 * @brief Функция потока таймера SVM.
 * Периодически обновляет счетчики BCB и линии для ВСЕХ активных экземпляров SVM.
 * Защищает доступ к счетчикам экземпляров с помощью их собственных мьютексов.
 * Ожидает с использованием pthread_cond_timedwait на глобальной cond var.
 * @param arg Указатель на массив SvmInstance[MAX_SVM_INSTANCES].
 * @return NULL.
 */
void* timer_thread_func(void* arg);


/**
 * @brief Устанавливает глобальный флаг завершения и сигнализирует потоку таймера.
 * Эта функция потокобезопасна.
 */
void stop_timer_thread_signal(void);

/**
 * @brief Получает текущее значение счетчика BCB для конкретного экземпляра (потокобезопасно).
 * @param instance Указатель на экземпляр SVM.
 * @return Текущее значение BCB.
 */
uint32_t get_instance_bcb_counter(SvmInstance *instance);

/**
 * @brief Получает текущие значения счетчиков состояния линии для конкретного экземпляра (потокобезопасно).
 * @param instance Указатель на экземпляр SVM.
 * @param kla Указатель для записи значения KLA (может быть NULL).
 * @param sla_us100 Указатель для записи значения SLA в 1/100 мкс (может быть NULL).
 * @param ksa Указатель для записи значения KSA (может быть NULL).
 */
void get_instance_line_status_counters(SvmInstance *instance, uint16_t *kla, uint32_t *sla_us100, uint16_t *ksa);

#endif // SVM_TIMERS_H