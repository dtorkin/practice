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
#include <limits.h> // Для UINT16_MAX, UINT32_MAX

// --- Константы для таймеров и вывода ---
#define TIMER_INTERVAL_BCB_MS 50           // Интервал основного таймера BCB
#define TIMER_INTERVAL_LINK_STATUS_MS 2000 // Частота эмуляции проверки статуса линии
#define LINK_CHANGE_PROBABILITY 2          // Вероятность изменения LinkUp (1/X)
#define LINK_LOW_PROBABILITY 10            // Вероятность увеличения времени низкого уровня (1/Y)
#define SIGN_DET_CHANGE_PROBABILITY 3      // Вероятность изменения SignDet (1/Z)
#define COUNTER_PRINT_INTERVAL_SEC 5       // <-- ДОБАВЛЕНО: Интервал вывода счетчиков

// --- Внешние переменные счетчиков (объявляем как extern) ---
extern volatile uint32_t bcbCounter;
extern uint16_t linkUpChangesCounter;
extern uint32_t linkUpLowTimeSeconds; // Был int, должен быть uint32_t для 4 байт
extern uint16_t signDetChangesCounter;

// --- Функции ---

int start_timer(int interval_ms, void (*handler)(int), int sig);
void bcbTimerHandler(int sig);
void print_counters(void);

#endif // SVM_TIMERS_H