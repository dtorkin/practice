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

int init_svm_timer_sync(void) {
    if (pthread_mutex_init(&svm_timer_management_mutex, NULL) != 0) {
        perror("Failed to initialize timer management mutex");
        return -1;
    }
     if (pthread_cond_init(&svm_timer_cond, NULL) != 0) {
        perror("Failed to initialize timer condition variable");
        pthread_mutex_destroy(&svm_timer_management_mutex);
        return -1;
    }
    srand(time(NULL)); // Инициализация ГСЧ
    return 0;
}

void destroy_svm_timer_sync(void) {
    pthread_mutex_destroy(&svm_timer_management_mutex);
    pthread_cond_destroy(&svm_timer_cond);
}

void stop_timer_thread_signal(void) {
    pthread_mutex_lock(&svm_timer_management_mutex);
    global_timer_keep_running = false;
    pthread_cond_signal(&svm_timer_cond); // Разбудить таймер, если он спит
    pthread_mutex_unlock(&svm_timer_management_mutex);
    printf("Timer thread stop signaled.\n");
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

void* timer_thread_func(void* arg) {
    SvmInstance *instances = (SvmInstance*)arg;
    if (!instances) {
        fprintf(stderr, "Timer thread: Invalid argument (NULL instances array).\n");
        return NULL;
    }

    struct timespec next_wake_time;
    int rc = 0;

    printf("SVM Timer thread started.\n");

    pthread_mutex_lock(&svm_timer_management_mutex);
    global_timer_keep_running = true; // Устанавливаем флаг под мьютексом
    pthread_mutex_unlock(&svm_timer_management_mutex);


    clock_gettime(CLOCK_REALTIME, &next_wake_time);

    pthread_mutex_lock(&svm_timer_management_mutex); // Захватываем мьютекс перед циклом
    while (global_timer_keep_running) {
        // Рассчитываем следующее время пробуждения
        long nsec = next_wake_time.tv_nsec + (TIMER_INTERVAL_BCB_MS % 1000) * 1000000L;
        next_wake_time.tv_sec += TIMER_INTERVAL_BCB_MS / 1000 + nsec / 1000000000L;
        next_wake_time.tv_nsec = nsec % 1000000000L;

        // Ожидаем на глобальной условной переменной
        rc = pthread_cond_timedwait(&svm_timer_cond, &svm_timer_management_mutex, &next_wake_time);

        if (!global_timer_keep_running) { // Проверяем флаг после пробуждения
            break; // Выходим, если пришел сигнал остановки
        }

        // Если проснулись по таймауту, обновляем счетчики экземпляров
        if (rc == ETIMEDOUT) {
            // Отпускаем глобальный мьютекс перед доступом к мьютексам экземпляров
            pthread_mutex_unlock(&svm_timer_management_mutex);

            // Обновляем счетчики для всех активных экземпляров
            for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
                 // Блокируем мьютекс конкретного экземпляра
                 pthread_mutex_lock(&instances[i].instance_mutex);
                 // Обновляем только если экземпляр активен
                 if (instances[i].is_active) {
                      // Обновление BCB
                      instances[i].bcb_counter++;

                      // Обновление счетчиков линии
                      instances[i].link_status_timer_counter++;
                      if (instances[i].link_status_timer_counter >= (TIMER_INTERVAL_LINK_STATUS_MS / TIMER_INTERVAL_BCB_MS)) {
                          instances[i].link_status_timer_counter = 0; // Сброс счетчика интервала

                          // Эмуляция изменения LinkUp
                          if (rand() % LINK_CHANGE_PROBABILITY == 0) {
                               if (instances[i].link_up_changes_counter < UINT16_MAX) {
                                   instances[i].link_up_changes_counter++;
                               }
                               // Эмуляция увеличения времени низкого уровня (SLA)
                               if (rand() % LINK_LOW_PROBABILITY == 0) {
                                   // Увеличиваем на интервал проверки * 10 (для получения единиц 1/100 мкс)
                                   uint32_t increment = (uint32_t)TIMER_INTERVAL_LINK_STATUS_MS * 10;
                                   if (instances[i].link_up_low_time_us100 <= UINT32_MAX - increment) {
                                       instances[i].link_up_low_time_us100 += increment;
                                   } else {
                                       instances[i].link_up_low_time_us100 = UINT32_MAX; // Предотвращение переполнения
                                   }
                               }
                          }
                          // Эмуляция изменения SignDet
                          if (rand() % SIGN_DET_CHANGE_PROBABILITY == 0) {
                               if (instances[i].sign_det_changes_counter < UINT16_MAX) {
                                   instances[i].sign_det_changes_counter++;
                               }
                          }
                      }
                 }
                 // Отпускаем мьютекс экземпляра
                 pthread_mutex_unlock(&instances[i].instance_mutex);
            } // end for

            // Снова захватываем глобальный мьютекс перед следующей итерацией timedwait
            pthread_mutex_lock(&svm_timer_management_mutex);

        } else if (rc == 0) {
            // Пробуждение по сигналу (stop_timer_thread_signal), флаг global_timer_keep_running проверится в начале цикла
            // printf("Timer thread woken by signal.\n");
        } else {
            perror("pthread_cond_timedwait failed in timer thread");
            global_timer_keep_running = false; // Принудительный выход при ошибке
        }
    } // end while

    pthread_mutex_unlock(&svm_timer_management_mutex); // Отпускаем мьютекс при выходе
    printf("SVM Timer thread stopped.\n");
    return NULL;
}

// Функция print_counters удалена, так как счетчики теперь по экземплярам.
// Вывод можно организовать в main или отдельной функции, итерируя по instances.