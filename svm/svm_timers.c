/*
 * svm/svm_timers.c
 *
 * Описание:
 * Реализация логики потока таймера и потокобезопасного доступа к счетчикам SVM.
 */

#include "svm_timers.h" // <-- ДОБАВИТЬ ЭТОТ INCLUDE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h> // Добавил на всякий случай для UINT*_MAX

// --- Определения глобальных переменных ---
volatile uint32_t bcbCounter = 0;
uint16_t linkUpChangesCounter = 0;
uint32_t linkUpLowTimeSeconds = 0;
uint16_t signDetChangesCounter = 0;

pthread_mutex_t svm_counters_mutex;
pthread_cond_t svm_timer_cond;
volatile bool timer_thread_running = false;


// --- Реализация функций ---

int init_svm_counters_mutex_and_cond(void) {
    if (pthread_mutex_init(&svm_counters_mutex, NULL) != 0) {
        perror("Failed to initialize counters mutex");
        return -1;
    }
     if (pthread_cond_init(&svm_timer_cond, NULL) != 0) {
        perror("Failed to initialize timer condition variable");
        pthread_mutex_destroy(&svm_counters_mutex);
        return -1;
    }
    srand(time(NULL));
    return 0;
}

void destroy_svm_counters_mutex_and_cond(void) {
    pthread_mutex_destroy(&svm_counters_mutex);
    pthread_cond_destroy(&svm_timer_cond);
}

void stop_timer_thread(void) {
    pthread_mutex_lock(&svm_counters_mutex);
    timer_thread_running = false;
    pthread_cond_signal(&svm_timer_cond);
    pthread_mutex_unlock(&svm_counters_mutex);
}

uint32_t get_bcb_counter(void) {
    uint32_t val;
    pthread_mutex_lock(&svm_counters_mutex);
    val = bcbCounter;
    pthread_mutex_unlock(&svm_counters_mutex);
    return val;
}

void get_line_status_counters(uint16_t *kla, uint32_t *sla, uint16_t *ksa) {
     pthread_mutex_lock(&svm_counters_mutex);
     if (kla) *kla = linkUpChangesCounter;
     if (sla) *sla = linkUpLowTimeSeconds;
     if (ksa) *ksa = signDetChangesCounter;
     pthread_mutex_unlock(&svm_counters_mutex);
}

void* timer_thread_func(void* arg) {
    (void)arg;
    struct timespec next_wake_time;
    int rc = 0;

    printf("SVM Timer thread started.\n");
    pthread_mutex_lock(&svm_counters_mutex);
    timer_thread_running = true;

    clock_gettime(CLOCK_REALTIME, &next_wake_time);

    while (timer_thread_running) {
        // Рассчитываем следующее время пробуждения
        // Теперь константы TIMER_INTERVAL_BCB_MS видны из svm_timers.h
        long nsec = next_wake_time.tv_nsec + (TIMER_INTERVAL_BCB_MS % 1000) * 1000000L;
        next_wake_time.tv_sec += TIMER_INTERVAL_BCB_MS / 1000 + nsec / 1000000000L;
        next_wake_time.tv_nsec = nsec % 1000000000L;

        rc = pthread_cond_timedwait(&svm_timer_cond, &svm_counters_mutex, &next_wake_time);

        if (!timer_thread_running) {
            break;
        }

        if (rc == ETIMEDOUT) {
            bcbCounter++;
            static int linkStatusTimerCounter = 0;
            linkStatusTimerCounter++;
             // Теперь константы TIMER_INTERVAL_LINK_STATUS_MS и т.д. видны
            if (linkStatusTimerCounter >= (TIMER_INTERVAL_LINK_STATUS_MS / TIMER_INTERVAL_BCB_MS)) {
                linkStatusTimerCounter = 0;
                if (rand() % LINK_CHANGE_PROBABILITY == 0) {
                     if (linkUpChangesCounter < UINT16_MAX) linkUpChangesCounter++;
                     if (rand() % LINK_LOW_PROBABILITY == 0) {
                         uint32_t increment = (TIMER_INTERVAL_LINK_STATUS_MS * 10);
                         if (linkUpLowTimeSeconds <= UINT32_MAX - increment) linkUpLowTimeSeconds += increment;
                         else linkUpLowTimeSeconds = UINT32_MAX;
                     }
                }
                 if (rand() % SIGN_DET_CHANGE_PROBABILITY == 0) {
                     if (signDetChangesCounter < UINT16_MAX) signDetChangesCounter++;
                 }
             }

        } else if (rc == 0) {
            // printf("Timer thread woken by signal.\n"); // Можно раскомментировать для отладки
        } else {
            perror("pthread_cond_timedwait failed in timer thread");
            timer_thread_running = false;
        }
    }

    pthread_mutex_unlock(&svm_counters_mutex);
    printf("SVM Timer thread stopped.\n");
    return NULL;
}


void print_counters(void) {
    pthread_mutex_lock(&svm_counters_mutex);
	printf("\n--- Счетчики SVM ---\n");
	printf("Счетчик BCB: 0x%08X (%u)\n", bcbCounter, bcbCounter);
	printf("Изменения LinkUp (KLA): %u\n", linkUpChangesCounter);
	printf("Время низкого уровня LinkUp (SLA): %u (ед. 1/100 мкс)\n", linkUpLowTimeSeconds);
	printf("Изменения SignDet (KSA): %u\n", signDetChangesCounter);
	printf("--- Конец счетчиков ---\n");
    pthread_mutex_unlock(&svm_counters_mutex);
}