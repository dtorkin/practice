/*
 * uvm/uvm_types.h
 * Описание: Типы данных, специфичные для UVM.
 * (Модифицировано для поддержки нескольких SVM)
 */
#ifndef UVM_TYPES_H
#define UVM_TYPES_H

#include <stdint.h>
#include <stdbool.h> // Для bool
#include "../protocol/protocol_defs.h" // Для Message, LogicalAddress
#include "../io/io_interface.h" // Для IOInterface
#include "../config/config.h" // Для MAX_SVM_CONFIGS

// Предварительное объявление очереди ответов
struct ThreadSafeUvmRespQueue;

typedef enum {
    UVM_REQ_NONE = 0,         // Добавим значение для "нет запроса" или неизвестного
    UVM_REQ_SEND_MESSAGE,   // Просто отправить сообщение (уже было)

    // --- Добавляем константы для конкретных команд ---
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
    // --- Конец добавленных констант ---

    UVM_REQ_CONNECT,        // Установить соединение (уже было)
    UVM_REQ_DISCONNECT,     // Разорвать соединение (уже было)
    UVM_REQ_SHUTDOWN        // Сигнал Sender'у завершиться (уже было)
} UvmRequestType;

// Структура запроса к Sender'у UVM
typedef struct {
    UvmRequestType type;
    int target_svm_id; // <-- ДОБАВЛЕНО: ID целевого SVM (индекс в svm_links)
    Message message;   // Сообщение для отправки (если type == UVM_REQ_SEND_MESSAGE)
    // Другие параметры по необходимости
} UvmRequest;

// Структура для сообщений в очереди ответов от Receiver'ов к Main
typedef struct {
    int source_svm_id; // ID SVM, от которого пришло сообщение
    Message message;   // Само сообщение
} UvmResponseMessage;

// Статус соединения с SVM
typedef enum {
    UVM_LINK_INACTIVE,      // Неактивно / Не подключено
    UVM_LINK_CONNECTING,    // В процессе подключения
    UVM_LINK_ACTIVE,        // Подключено и работает
    UVM_LINK_FAILED,        // Ошибка (соединения, чтения, записи)
    UVM_LINK_DISCONNECTING // В процессе отключения
} UvmLinkStatus;

// Структура для хранения состояния связи с одним SVM
typedef struct {
    int id;                 // ID этого слота (0..MAX_SVM_CONFIGS-1)
    IOInterface *io_handle; // Указатель на созданный IO интерфейс для этого соединения
    int connection_handle;  // Дескриптор сокета/файла
    UvmLinkStatus status;   // Текущий статус соединения
    LogicalAddress assigned_lak; // Ожидаемый/подтвержденный LAK этого SVM
    pthread_t receiver_tid; // ID потока-приемника для этого соединения
    // Можно добавить счетчики или другую статистику
    time_t last_activity_time; // Время последней активности (для Keep-Alive)

} UvmSvmLink;


#endif // UVM_TYPES_H