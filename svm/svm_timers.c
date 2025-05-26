/*
 * svm/svm_timers.c
 *
 * Описание:
 * Реализация логики потока таймера и потокобезопасного доступа
 * к счетчикам для МНОЖЕСТВА экземпляров SVM.
 */

#include "svm_timers.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h> // Для UINT*_MAX

// --- Глобальные переменные для синхронизации таймера ---
pthread_mutex_t svm_timer_management_mutex;
pthread_cond_t svm_timer_cond;
volatile bool global_timer_keep_running = false; // Инициализируем как false

// --- Реализация функций ---

// Персональный таймер для одного экземпляра SVM
void* svm_instance_timer_thread_func(void* arg) {
    SvmInstance *instance = (SvmInstance*)arg;
    if (!instance) {
        fprintf(stderr, "InstanceTimer (ID %d): Invalid argument (NULL instance).\n", instance ? instance->id : -1);
        return NULL;
    }

    printf("InstanceTimer (ID %d, LAK 0x%02X): Thread started.\n", instance->id, instance->assigned_lak);

    // Для usleep интервал в микросекундах
    useconds_t sleep_interval_us = TIMER_INTERVAL_BCB_MS * 1000;

    while (instance->personal_timer_keep_running && keep_running) {
        usleep(sleep_interval_us); // Спим заданный интервал

        // Проверяем флаги снова после сна, на случай если их изменили во время сна
        if (!(instance->personal_timer_keep_running && keep_running)) {
            break;
        }

        pthread_mutex_lock(&instance->instance_mutex); // Блокируем мьютекс этого экземпляра

        // Обновляем BCB
        if (instance->bcb_counter < UINT32_MAX) {
            instance->bcb_counter++;
        } else {
            instance->bcb_counter = 0; // Переполнение
        }

        // Обновление счетчиков линии (KLA, SLA, KSA)
		instance->link_status_timer_counter++;
		if (instance->link_status_timer_counter >= (TIMER_INTERVAL_LINK_STATUS_MS / TIMER_INTERVAL_BCB_MS)) {
			instance->link_status_timer_counter = 0; // Сброс

            if (rand() % LINK_CHANGE_PROBABILITY == 0) {
                if (instance->link_up_changes_counter < UINT16_MAX) {
                    instance->link_up_changes_counter++;
                }
                if (rand() % LINK_LOW_PROBABILITY == 0) {
                    uint32_t increment = (uint32_t)TIMER_INTERVAL_LINK_STATUS_MS * 10;
                    if (instance->link_up_low_time_us100 <= UINT32_MAX - increment) {
                        instance->link_up_low_time_us100 += increment;
                    } else {
                        instance->link_up_low_time_us100 = UINT32_MAX;
                    }
                }
            }
            if (rand() % SIGN_DET_CHANGE_PROBABILITY == 0) {
                if (instance->sign_det_changes_counter < UINT16_MAX) {
                    instance->sign_det_changes_counter++;
                }
            }
        }
        pthread_mutex_unlock(&instance->instance_mutex); // Отпускаем мьютекс
    }

    printf("InstanceTimer (ID %d, LAK 0x%02X): Thread finished.\n", instance->id, instance->assigned_lak);
    return NULL;
}

uint32_t get_instance_bcb_counter(SvmInstance *instance) {
    if (!instance) return 0;
    uint32_t val;
    pthread_mutex_lock(&instance->instance_mutex);
    val = instance->bcb_counter;
    pthread_mutex_unlock(&instance->instance_mutex);
    return val;
}

void get_instance_line_status_counters(SvmInstance *instance, uint16_t *kla, uint32_t *sla_us100, uint16_t *ksa) {
     if (!instance) return;
     pthread_mutex_lock(&instance->instance_mutex);
     if (kla) *kla = instance->link_up_changes_counter;
     if (sla_us100) *sla_us100 = instance->link_up_low_time_us100; // Используем новое имя
     if (ksa) *ksa = instance->sign_det_changes_counter;
     pthread_mutex_unlock(&instance->instance_mutex);
}

// Глобальный init_svm_timer_sync теперь может быть проще или только для srand
int init_svm_app_wide_resources(void) { // Переименовал
    srand(time(NULL));
    printf("SVM App-wide resources (srand) initialized.\n");
    return 0;
}

void destroy_svm_app_wide_resources(void) { // Переименовал
    printf("SVM App-wide resources destroyed.\n");
}