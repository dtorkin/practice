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

// Типы запросов от Main к Sender'у UVM
typedef enum {
    UVM_REQ_SEND_MESSAGE,   // Просто отправить сообщение
    UVM_REQ_CONNECT,        // Установить соединение (может быть не нужно, main сам соединяется)
    UVM_REQ_DISCONNECT,     // Разорвать соединение
    UVM_REQ_SHUTDOWN        // Сигнал Sender'у завершиться
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
    uint32_t last_message_time; // Время последнего сообщения (для таймаутов)

} UvmSvmLink;


#endif // UVM_TYPES_H