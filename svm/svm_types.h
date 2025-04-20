/*
 * svm/svm_types.h
 *
 * Описание:
 * Определяет типы данных, специфичные для многоэкземплярного SVM,
 * включая структуру состояния экземпляра и структуру сообщения для очередей.
 */

#ifndef SVM_TYPES_H
#define SVM_TYPES_H

#include <pthread.h>
#include <stdbool.h>
#include "../protocol/protocol_defs.h"
#include "../utils/ts_queued_msg_queue.h"
#include "../utils/ts_queued_msg_queue_fwd.h" // Используем предварительное объявление для ThreadSafeQueuedMsgQueue
#include "../io/io_interface.h"

// Максимальное количество эмулируемых экземпляров СВ-М
#define MAX_SVM_INSTANCES 4

// Предварительное объявление ThreadSafeQueue, чтобы избежать циклической зависимости
// Полное определение будет в ts_queue.h
// typedef struct ThreadSafeQueue ThreadSafeQueue; // Заменено на ts_queue_fwd.h

// Состояние одного экземпляра СВ-М
typedef struct SvmInstance {
    int id;                      // Уникальный ID экземпляра (0 to MAX_SVM_INSTANCES-1)
    pthread_t receiver_tid;      // ID потока-приемника для этого экземпляра (временно)
    pthread_t processor_tid;     // ID потока-обработчика для этого экземпляра (временно)
    // Sender и Timer будут общими

    IOInterface *io_handle;      // Указатель на общий IO интерфейс
    int client_handle;           // Дескриптор сокета/файла клиента
    bool is_active;              // Флаг, активен ли экземпляр (есть ли клиент)
    LogicalAddress assigned_lak; // Назначенный ЛАК для этого СВ-М

	ThreadSafeQueuedMsgQueue *incoming_queue; // Входящая очередь QueuedMessage для этого экземпляра
    // Исходящая очередь будет общей (svm_outgoing_queue в svm_main)

    // --- Состояние, специфичное для экземпляра ---
    SVMState current_state;        // Текущее состояние (NOT_INITIALIZED, INITIALIZED, SELF_TEST)
    uint16_t message_counter;      // Счетчик исходящих сообщений этого экземпляра
    // --- Счетчики, специфичные для экземпляра (ранее в svm_timers.c) ---
    volatile uint32_t bcb_counter;
    uint16_t link_up_changes_counter;
    uint32_t link_up_low_time_us100; // Имя изменено для ясности единиц (1/100 мкс)
    uint16_t sign_det_changes_counter;
    int link_status_timer_counter; // Счетчик для эмуляции проверки линии

    pthread_mutex_t instance_mutex; // Мьютекс для защиты счетчиков и состояния этого экземпляра

} SvmInstance;

// Структура для передачи сообщений в очередях (содержит ID экземпляра)
typedef struct {
    int instance_id; // ID экземпляра, к которому относится сообщение
    Message message;     // Само сообщение (копия)
} QueuedMessage;


#endif // SVM_TYPES_H