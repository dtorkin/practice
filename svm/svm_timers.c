/*
 * svm/svm_timers.c
 *
 * Описание:
 * Реализация логики таймеров и счетчиков SVM.
 */

#include "svm_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>
#include <limits.h> // Для UINT16_MAX, UINT32_MAX

// --- Определения глобальных переменных счетчиков ---
volatile uint32_t bcbCounter = 0;
uint16_t linkUpChangesCounter = 0;
uint32_t linkUpLowTimeSeconds = 0; // Используем uint32_t
uint16_t signDetChangesCounter = 0;

// --- Константы для таймеров (можно вынести в svm_timers.h или config) ---
#define TIMER_INTERVAL_BCB_MS 50
#define TIMER_INTERVAL_LINK_STATUS_MS 2000 // Частота проверки Link статуса
#define LINK_CHANGE_PROBABILITY 2 // Вероятность изменения LinkUp (1/X)
#define LINK_LOW_PROBABILITY 10   // Вероятность увеличения времени низкого уровня (1/Y)
#define SIGN_DET_CHANGE_PROBABILITY 3 // Вероятность изменения SignDet (1/Z)

// --- Реализация функций ---

// Запуск таймера
int start_timer(int interval_ms, void (*handler)(int), int sig) {
	struct sigaction sa;
	struct itimerval timer;

    // Важно: Инициализация генератора случайных чисел один раз при старте
    srand(time(NULL));

	// Установка обработчика сигнала
	sa.sa_handler = handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART; // Добавляем SA_RESTART для автоматического перезапуска системных вызовов
	if (sigaction(sig, &sa, NULL) == -1) {
		perror("sigaction");
		return -1;
	}

	// Настройка таймера
	timer.it_value.tv_sec = interval_ms / 1000;
	timer.it_value.tv_usec = (interval_ms % 1000) * 1000;
	timer.it_interval.tv_sec = interval_ms / 1000;
	timer.it_interval.tv_usec = (interval_ms % 1000) * 1000;

	// Запуск таймера
	if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
		perror("setitimer");
		return -1;
	}
	return 0;
}


// Обработчик таймера BCB и эмуляции изменений линии
void bcbTimerHandler(int sig) {
	(void)sig; // Подавляем предупреждение о неиспользуемом параметре
	static int linkStatusTimerCounter = 0; // Счетчик для эмуляции проверки статуса линии

	// --- Обновление основного счетчика BCB ---
	// Потенциально опасное место для гонки данных при многопоточности!
	// Пока оставляем так, но при добавлении потоков нужно будет защитить мьютексом.
	bcbCounter++;

	// --- Эмуляция проверки статуса линии и обновления счетчиков KLA, SLA, KSA ---
	// Выполняется реже, чем основной таймер BCB
	linkStatusTimerCounter++;
	if (linkStatusTimerCounter >= (TIMER_INTERVAL_LINK_STATUS_MS / TIMER_INTERVAL_BCB_MS)) {
		linkStatusTimerCounter = 0; // Сброс счетчика интервала

		// Эмуляция изменения LinkUp (например, с вероятностью 1/LINK_CHANGE_PROBABILITY)
		if (rand() % LINK_CHANGE_PROBABILITY == 0) {
            if (linkUpChangesCounter < UINT16_MAX) { // Предотвращение переполнения
			    linkUpChangesCounter++;
            }
			// Эмуляция того, что линия иногда остается в низком уровне
			if (rand() % LINK_LOW_PROBABILITY == 0) {
                // Время низкого уровня увеличивается на интервал проверки
                if (linkUpLowTimeSeconds <= UINT32_MAX - (TIMER_INTERVAL_LINK_STATUS_MS / 10)) { // Предотвращение переполнения (делим на 10 для 1/100 мкс -> 1 сек = 100 * 10 мс)
				    linkUpLowTimeSeconds += (TIMER_INTERVAL_LINK_STATUS_MS / 10); // Увеличиваем на кол-во 1/100 мкс в интервале
                } else {
                    linkUpLowTimeSeconds = UINT32_MAX;
                }
			}
		}

		// Эмуляция изменения SignDet (например, с вероятностью 1/SIGN_DET_CHANGE_PROBABILITY)
		if (rand() % SIGN_DET_CHANGE_PROBABILITY == 0) {
            if (signDetChangesCounter < UINT16_MAX) { // Предотвращение переполнения
			    signDetChangesCounter++;
            }
		}
	}
}

// Вывод счетчиков SVM
void print_counters(void) { // Изменено на void, т.к. использует глобальные переменные
	printf("\n--- Счетчики SVM ---\n");
	printf("Счетчик BCB: 0x%08X (%u)\n", bcbCounter, bcbCounter);
	printf("Изменения LinkUp (KLA): %u\n", linkUpChangesCounter);
	printf("Время низкого уровня LinkUp (SLA): %u (ед. 1/100 мкс)\n", linkUpLowTimeSeconds); // Уточнили единицы
	printf("Изменения SignDet (KSA): %u\n", signDetChangesCounter);
	printf("--- Конец счетчиков ---\n");
}