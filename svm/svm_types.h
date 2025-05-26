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

// Структура для хранения состояния связи с одним SVM
typedef struct SvmInstance {
    // --- Основные поля (как были) ---
    int id;
    pthread_t receiver_tid;
    pthread_t processor_tid;
    pthread_t timer_tid; // <--- НОВОЕ ПОЛЕ для ID персонального таймер-потока
    IOInterface *io_handle; // Указатель на IO интерфейс listener'а этого экземпляра
    int client_handle;
    bool is_active;
    LogicalAddress assigned_lak;
    struct ThreadSafeQueuedMsgQueue *incoming_queue; // Используем предварительное объявление

    // --- Состояние, специфичное для экземпляра ---
    SVMState current_state;
    uint16_t message_counter; // Счетчик исходящих сообщений
    // --- Счетчики ---
    volatile uint32_t bcb_counter;
    uint16_t link_up_changes_counter;
    uint32_t link_up_low_time_us100;
    uint16_t sign_det_changes_counter;
    int link_status_timer_counter;

    pthread_mutex_t instance_mutex;
	
    // --- Флаг для управления персональным таймером ---
    volatile bool personal_timer_keep_running; // <--- НОВЫЙ ФЛАГ

    // --- ПОЛЯ ДЛЯ ИМИТАЦИИ СБОЕВ ---
	bool user_flag1; // Для кастомной логики сбоев (например, прекратить отвечать)
    bool simulate_control_failure;
    int disconnect_after_messages; // -1 = off
    int messages_sent_count;      // Счетчик отправленных для disconnect_after
    bool simulate_response_timeout;
    bool send_warning_on_confirm;
    uint8_t warning_tks;
    // -----------------------------------------

} SvmInstance;

// Структура для передачи сообщений в очередях
typedef struct {
    int instance_id;
    Message message;
} QueuedMessage;


#endif // SVM_TYPES_H