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
// #include "../utils/ts_queued_msg_queue_fwd.h" // <-- УДАЛЕНО
#include "../io/io_interface.h"

// Максимальное количество эмулируемых экземпляров СВ-М
#define MAX_SVM_INSTANCES 4

// Предварительное объявление структуры очереди
struct ThreadSafeQueuedMsgQueue;

// Состояние одного экземпляра СВ-М
typedef struct SvmInstance {
    int id;
    pthread_t receiver_tid;
    pthread_t processor_tid;
    IOInterface *io_handle;
    int client_handle;
    bool is_active;
    LogicalAddress assigned_lak;

    // Используем указатель на предварительно объявленную структуру
    struct ThreadSafeQueuedMsgQueue *incoming_queue;

    // --- Состояние, специфичное для экземпляра ---
    SVMState current_state;
    uint16_t message_counter;
    // --- Счетчики, специфичные для экземпляра ---
    volatile uint32_t bcb_counter;
    uint16_t link_up_changes_counter;
    uint32_t link_up_low_time_us100;
    uint16_t sign_det_changes_counter;
    int link_status_timer_counter;

    pthread_mutex_t instance_mutex;

} SvmInstance;

// Структура для передачи сообщений в очередях
typedef struct {
    int instance_id;
    Message message;
} QueuedMessage;


#endif // SVM_TYPES_H