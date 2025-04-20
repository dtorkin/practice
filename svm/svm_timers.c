/*
 * svm/svm_timers.c
 * Описание: Реализация логики потока таймера и доступа к ГЛОБАЛЬНЫМ счетчикам SVM.
 * (Возвращено к одно-экземплярной модели)
 */
#include "svm_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>

// Определения глобальных переменных
volatile uint32_t bcbCounter = 0;
uint16_t linkUpChangesCounter = 0;
uint32_t linkUpLowTimeUs100 = 0; // Используем корректное имя
uint16_t signDetChangesCounter = 0;
int linkStatusTimerCounter = 0; // Счетчик для эмуляции

pthread_mutex_t svm_counters_mutex;
pthread_cond_t svm_timer_cond;
volatile bool timer_thread_running = false; // Флаг для цикла

int init_svm_counters_mutex_and_cond(void) {
    if (pthread_mutex_init(&svm_counters_mutex, NULL) != 0) { /*...*/ return -1; }
    if (pthread_cond_init(&svm_timer_cond, NULL) != 0) { /*...*/ pthread_mutex_destroy(&svm_counters_mutex); return -1; }
    srand(time(NULL));
    timer_thread_running = false; // Сброс флага при инициализации
    return 0;
}

void destroy_svm_counters_mutex_and_cond(void) {
    pthread_mutex_destroy(&svm_counters_mutex);
    pthread_cond_destroy(&svm_timer_cond);
}

// Сигнализирует таймеру остановиться
void stop_timer_thread(void) {
    pthread_mutex_lock(&svm_counters_mutex);
    timer_thread_running = false;
    pthread_cond_signal(&svm_timer_cond);
    pthread_mutex_unlock(&svm_counters_mutex);
    printf("Timer thread stop signaled.\n");
}

uint32_t get_bcb_counter(void) {
    uint32_t val;
    pthread_mutex_lock(&svm_counters_mutex);
    val = bcbCounter;
    pthread_mutex_unlock(&svm_counters_mutex);
    return val;
}

void get_line_status_counters(uint16_t *kla, uint32_t *sla_us100, uint16_t *ksa) {
     pthread_mutex_lock(&svm_counters_mutex);
     if (kla) *kla = linkUpChangesCounter;
     if (sla_us100) *sla_us100 = linkUpLowTimeUs100;
     if (ksa) *ksa = signDetChangesCounter;
     pthread_mutex_unlock(&svm_counters_mutex);
}

void* timer_thread_func(void* arg) {
    (void)arg; // Аргумент не используется
    struct timespec next_wake_time;
    int rc = 0;

    printf("SVM Timer thread started.\n");

    pthread_mutex_lock(&svm_counters_mutex);
    timer_thread_running = true; // Устанавливаем флаг работы
    clock_gettime(CLOCK_REALTIME, &next_wake_time);

    while (timer_thread_running) {
        // Рассчитываем следующее время пробуждения
        long nsec = next_wake_time.tv_nsec + (TIMER_INTERVAL_BCB_MS % 1000) * 1000000L;
        next_wake_time.tv_sec += TIMER_INTERVAL_BCB_MS / 1000 + nsec / 1000000000L;
        next_wake_time.tv_nsec = nsec % 1000000000L;

        // Ожидаем на условной переменной
        rc = pthread_cond_timedwait(&svm_timer_cond, &svm_counters_mutex, &next_wake_time);

        if (!timer_thread_running) { // Проверяем флаг после пробуждения
            break;
        }

        if (rc == ETIMEDOUT) {
            // Обновляем глобальные счетчики
            bcbCounter++;
            linkStatusTimerCounter++;
            if (linkStatusTimerCounter >= (TIMER_INTERVAL_LINK_STATUS_MS / TIMER_INTERVAL_BCB_MS)) {
                linkStatusTimerCounter = 0;
                if (rand() % LINK_CHANGE_PROBABILITY == 0) {
                     if (linkUpChangesCounter < UINT16_MAX) linkUpChangesCounter++;
                     if (rand() % LINK_LOW_PROBABILITY == 0) {
                         uint32_t increment = (uint32_t)TIMER_INTERVAL_LINK_STATUS_MS * 10;
                         if (linkUpLowTimeUs100 <= UINT32_MAX - increment) linkUpLowTimeUs100 += increment;
                         else linkUpLowTimeUs100 = UINT32_MAX;
                     }
                }
                 if (rand() % SIGN_DET_CHANGE_PROBABILITY == 0) {
                     if (signDetChangesCounter < UINT16_MAX) signDetChangesCounter++;
                 }
             }
        } else if (rc != 0) { // Ошибка timedwait
            perror("pthread_cond_timedwait failed in timer thread");
            timer_thread_running = false; // Выходим при ошибке
        }
        // Если rc == 0, значит проснулись по сигналу stop_timer_thread,
        // флаг timer_thread_running будет проверен в начале следующей итерации.
    }

    pthread_mutex_unlock(&svm_counters_mutex);
    printf("SVM Timer thread stopped.\n");
    return NULL;
}

// Функция вывода глобальных счетчиков
void print_counters(void) {
    pthread_mutex_lock(&svm_counters_mutex);
    printf("\n--- SVM Counters ---\n");
    printf("BCB Counter: 0x%08X (%u)\n", bcbCounter, bcbCounter);
    printf("LinkUp Changes (KLA): %u\n", linkUpChangesCounter);
    printf("LinkUp Low Time (SLA): %u (x 1/100 us)\n", linkUpLowTimeUs100);
    printf("SignDet Changes (KSA): %u\n", signDetChangesCounter);
    printf("--- End Counters ---\n");
    pthread_mutex_unlock(&svm_counters_mutex);
}