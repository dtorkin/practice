/*
 * svm/svm_timers.h
 *
 * Описание:
 * Объявления для управления таймером и счетчиками для МНОЖЕСТВА экземпляров SVM.
 * (Добавлена обертка extern "C" для C++ совместимости)
 */

#ifndef SVM_TIMERS_H
#define SVM_TIMERS_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
// Включаем svm_types.h ДО extern "C", т.к. SvmInstance используется в прототипах
#include "svm_types.h"

// --- Константы для таймеров ---
#define TIMER_INTERVAL_BCB_MS 50
#define TIMER_INTERVAL_LINK_STATUS_MS 2000
#define LINK_CHANGE_PROBABILITY 2
#define LINK_LOW_PROBABILITY 10
#define SIGN_DET_CHANGE_PROBABILITY 3

// --- Глобальные переменные для таймера ---
// Эти объявления видны и C, и C++ коду
extern pthread_mutex_t svm_timer_management_mutex;
extern pthread_cond_t svm_timer_cond;
extern volatile bool global_timer_keep_running; // Переименовали из timer_thread_running

// --- Обертка для C++ для объявления функций с C-связыванием ---
#ifdef __cplusplus
extern "C" {
#endif

// --- Прототипы функций (C linkage) ---

/**
 * @brief Инициализирует глобальный мьютекс и условную переменную для таймера.
 */
int init_svm_timer_sync(void);

/**
 * @brief Уничтожает глобальный мьютекс и условную переменную таймера.
 */
void destroy_svm_timer_sync(void);

/**
 * @brief Функция потока таймера SVM. Обновляет счетчики для всех экземпляров.
 */
void* timer_thread_func(void* arg); // Принимает SvmInstance* (массив)

/**
 * @brief Сигнализирует потоку таймера о завершении.
 */
void stop_timer_thread_signal(void); // Переименовали из stop_timer_thread

/**
 * @brief Получает счетчик BCB для экземпляра.
 */
uint32_t get_instance_bcb_counter(SvmInstance *instance);

/**
 * @brief Получает счетчики состояния линии для экземпляра.
 */
void get_instance_line_status_counters(SvmInstance *instance, uint16_t *kla, uint32_t *sla_us100, uint16_t *ksa);

#ifdef __cplusplus
} // extern "C"
#endif
// --- Конец обертки ---

#endif // SVM_TIMERS_H