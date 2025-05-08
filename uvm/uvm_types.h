/*
 * uvm/uvm_types.h
 * Описание: Типы данных, специфичные для UVM.
 * (Добавлены поля для передачи статуса в GUI)
 */
#ifndef UVM_TYPES_H
#define UVM_TYPES_H

#include <stdint.h>
#include <stdbool.h> // Для bool
#include <pthread.h> // Для pthread_t
#include <time.h>    // Для time_t

#include "../protocol/protocol_defs.h" // Для Message, LogicalAddress, MessageType
#include "../io/io_interface.h" // Для IOInterface
#include "../config/config.h" // Для MAX_SVM_INSTANCES

// Предварительное объявление очереди ответов
struct ThreadSafeUvmRespQueue;

// Типы запросов от Main к Sender'у UVM
typedef enum {
    UVM_REQ_NONE = 0,
    UVM_REQ_SEND_MESSAGE,
    UVM_REQ_INIT_CHANNEL,
    UVM_REQ_PROVESTI_KONTROL,
    UVM_REQ_VYDAT_REZULTATY,
    UVM_REQ_VYDAT_SOSTOYANIE,
    UVM_REQ_PRIYAT_PARAM_SO,
    UVM_REQ_PRIYAT_TIME_REF,
    UVM_REQ_PRIYAT_REPER,
    UVM_REQ_PRIYAT_PARAM_SDR,
    UVM_REQ_PRIYAT_PARAM_3TSO,
    UVM_REQ_PRIYAT_REF_AZIMUTH,
    UVM_REQ_PRIYAT_PARAM_TSD,
    UVM_REQ_PRIYAT_NAV_DANNYE,
    UVM_REQ_CONNECT,
    UVM_REQ_DISCONNECT,
    UVM_REQ_SHUTDOWN
} UvmRequestType;

// Структура запроса к Sender'у UVM
typedef struct {
    UvmRequestType type;
    int target_svm_id;
    Message message;
} UvmRequest;

// Структура для сообщений в очереди ответов от Receiver'ов к Main
typedef struct {
    int source_svm_id;
    Message message;
} UvmResponseMessage;

// Статус соединения с SVM
typedef enum {
    UVM_LINK_INACTIVE = 0, // Обязательно = 0 для memset
    UVM_LINK_CONNECTING,
    UVM_LINK_ACTIVE,
    UVM_LINK_FAILED,
    UVM_LINK_DISCONNECTING,
    UVM_LINK_WARNING // Добавим статус для некритичных ошибок
} UvmLinkStatus;

// Структура для хранения состояния связи с одним SVM
typedef struct {
    int id;
    IOInterface *io_handle;
    int connection_handle;
    UvmLinkStatus status;
    LogicalAddress assigned_lak; // Ожидаемый LAK
    pthread_t receiver_tid;
    time_t last_activity_time; // Время последней активности

    // --- Поля для GUI ---
    MessageType last_sent_msg_type;
    uint16_t    last_sent_msg_num;
    MessageType last_recv_msg_type;
    uint16_t    last_recv_msg_num;
    uint32_t    last_recv_bcb;
    uint16_t    last_recv_kla;
    uint32_t    last_recv_sla_us100;
    uint16_t    last_recv_ksa;
    uint8_t     last_control_rsk; // 0xFF - еще не было, 0x3F - OK, другое - ошибка
    uint8_t     last_warning_tks; // 0 - нет предупреждения
    time_t      last_warning_time;

} UvmSvmLink;

#endif // UVM_TYPES_H