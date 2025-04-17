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

// --- Константы для таймеров ---
#define TIMER_INTERVAL_BCB_MS 50           // Интервал основного таймера BCB
#define TIMER_INTERVAL_LINK_STATUS_MS 2000 // Частота эмуляции проверки статуса линии
#define LINK_CHANGE_PROBABILITY 2          // Вероятность изменения LinkUp (1/X)
#define LINK_LOW_PROBABILITY 10            // Вероятность увеличения времени низкого уровня (1/Y)
#define SIGN_DET_CHANGE_PROBABILITY 3      // Вероятность изменения SignDet (1/Z)


// --- Внешние переменные счетчиков (объявляем как extern) ---
extern volatile uint32_t bcbCounter;
extern uint16_t linkUpChangesCounter;
extern uint32_t linkUpLowTimeSeconds;
extern uint16_t signDetChangesCounter;

// --- Функции ---

/**
 * @brief Инициализирует и запускает периодический таймер.
 * @param interval_ms Интервал таймера в миллисекундах.
 * @param handler Указатель на функцию-обработчик сигнала таймера.
 * @param sig Номер сигнала для использования таймером (например, SIGALRM).
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int start_timer(int interval_ms, void (*handler)(int), int sig);

/**
 * @brief Обработчик сигнала таймера для обновления счетчиков SVM.
 * @param sig Номер сигнала (не используется).
 */
void bcbTimerHandler(int sig);

/**
 * @brief Выводит текущие значения счетчиков SVM в консоль.
 */
void print_counters(void);

#endif // SVM_TIMERS_H