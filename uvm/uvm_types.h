/*
 * uvm/uvm_types.h
 * Описание: Типы данных, специфичные для UVM.
 * (Модифицировано для поддержки нескольких SVM и расширенной информации для GUI)
 */
#ifndef UVM_TYPES_H
#define UVM_TYPES_H

#include <stdint.h>
#include <stdbool.h> // Для bool
#include <pthread.h> // Для pthread_t
#include <time.h>    // Для time_t

#include "../protocol/protocol_defs.h" // Для Message, LogicalAddress, MessageType
#include "../io/io_interface.h" // Для IOInterface
// #include "../config/config.h" // MAX_SVM_CONFIGS теперь берем из svm_types.h
#include "../svm/svm_types.h" // <-- ВКЛЮЧАЕМ для MAX_SVM_INSTANCES (вместо MAX_SVM_CONFIGS)

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
    UVM_REQ_CONNECT,        // Может быть не используется, если main сам соединяется
    UVM_REQ_DISCONNECT,     // Может быть не используется
    UVM_REQ_SHUTDOWN
} UvmRequestType;

// Структура запроса к Sender'у UVM
typedef struct {
    UvmRequestType type;
    int target_svm_id; // ID целевого SVM (индекс в svm_links)
    Message message;   // Сообщение для отправки
} UvmRequest;

// Структура для сообщений в очереди ответов от Receiver'ов к Main
typedef struct {
    int source_svm_id; // ID SVM, от которого пришло сообщение
    Message message;   // Само сообщение
} UvmResponseMessage;

// Статус соединения с SVM
typedef enum {
    UVM_LINK_INACTIVE = 0,
    UVM_LINK_CONNECTING,
    UVM_LINK_ACTIVE,
    UVM_LINK_FAILED,
    UVM_LINK_DISCONNECTING,
    UVM_LINK_WARNING // Для некритичных проблем
} UvmLinkStatus;

// Структура для хранения состояния связи с одним SVM
typedef struct UvmSvmLink {
    int id;                 // ID этого слота (0..MAX_SVM_INSTANCES-1)
    IOInterface *io_handle; // Указатель на созданный IO интерфейс
    int connection_handle;  // Дескриптор сокета/файла
    UvmLinkStatus status;   // Текущий статус соединения
    LogicalAddress assigned_lak; // Ожидаемый/подтвержденный LAK
    pthread_t receiver_tid; // ID потока-приемника
    time_t last_activity_time; // Время последней АКТИВНОСТИ (получения сообщения)

    // --- Поля для GUI и внутреннего отслеживания ---
    MessageType last_sent_msg_type;   // Тип последнего отправленного UVM сообщения этому SVM
    uint16_t    last_sent_msg_num;    // Номер последнего отправленного
    time_t      last_sent_msg_time;   // Время последней отправки

    MessageType last_recv_msg_type;   // Тип последнего полученного от SVM сообщения
    uint16_t    last_recv_msg_num;    // Номер последнего полученного
    time_t      last_recv_msg_time;   // Время последнего получения (дублирует last_activity_time, но можно оставить для явности)

    uint32_t    last_recv_bcb;        // Последний BCB из сообщений SVM
    // Поля для других счетчиков SVM, если решим их передавать (KLA, SLA, KSA)
    // uint16_t    last_recv_kla;
    // uint32_t    last_recv_sla_us100;
    // uint16_t    last_recv_ksa;

    uint8_t     last_control_rsk;     // Последний RSK из "Результаты контроля" (0xFF если не было)
    uint8_t     last_warning_tks;     // Последний TKS из "Предупреждение" (0 если не было)
    time_t      last_warning_time;    // Время последнего предупреждения

    // Флаги и счетчики для специфичных состояний/ошибок
    bool        timeout_detected;         // Был ли таймаут keep-alive
    bool        response_timeout_detected;// Был ли таймаут ожидания ответа на команду
    bool        lak_mismatch_detected;    // Был ли неверный LAK в ответе
    bool        control_failure_detected; // Был ли RSK != ожидаемого "ОК"
} UvmSvmLink;


#endif // UVM_TYPES_H