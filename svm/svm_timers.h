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
extern volatile bool keep_running;

// Прототип новой функции потока для персонального таймера
void* svm_instance_timer_thread_func(void* arg);

// Функции для инициализации/уничтожения общих ресурсов (если нужны, например, srand)
int init_svm_app_wide_resources(void); // Переименовано
void destroy_svm_app_wide_resources(void); // Переименовано

// Функции доступа к счетчикам остаются
uint32_t get_instance_bcb_counter(SvmInstance *instance);
void get_instance_line_status_counters(SvmInstance *instance, uint16_t *kla, uint32_t *sla_us100, uint16_t *ksa);

#endif // SVM_TIMERS_H