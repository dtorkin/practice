/*
 * svm/svm_timers.h
 * Описание: Объявления для управления таймером и ГЛОБАЛЬНЫМИ счетчиками SVM.
 * (Возвращено к одно-экземплярной модели)
 */
#ifndef SVM_TIMERS_H
#define SVM_TIMERS_H

#include <stdint.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
// SvmInstance больше не нужен

// Константы таймеров
#define TIMER_INTERVAL_BCB_MS 50
#define TIMER_INTERVAL_LINK_STATUS_MS 2000
#define LINK_CHANGE_PROBABILITY 2
#define LINK_LOW_PROBABILITY 10
#define SIGN_DET_CHANGE_PROBABILITY 3

// --- Глобальные переменные счетчиков (определены в svm_timers.c) ---
extern volatile uint32_t bcbCounter;
extern uint16_t linkUpChangesCounter;
extern uint32_t linkUpLowTimeUs100; // Используем корректное имя единиц
extern uint16_t signDetChangesCounter;

// Мьютекс и условная переменная для таймера и счетчиков
extern pthread_mutex_t svm_counters_mutex;
extern pthread_cond_t svm_timer_cond;
extern volatile bool timer_thread_running; // Флаг для управления потоком таймера

int init_svm_counters_mutex_and_cond(void);
void destroy_svm_counters_mutex_and_cond(void);
void* timer_thread_func(void* arg); // arg теперь не используется
void stop_timer_thread(void); // Возвращаем старое название

// Функции доступа к глобальным счетчикам
uint32_t get_bcb_counter(void);
void get_line_status_counters(uint16_t *kla, uint32_t *sla_us100, uint16_t *ksa);
void print_counters(void); // Можем вернуть функцию для вывода

#endif // SVM_TIMERS_H