/*
 * uvm/uvm_types.h
 *
 * Описание:
 * Типы данных, специфичные для UVM, например, для очередей запросов.
 */

#ifndef UVM_TYPES_H
#define UVM_TYPES_H

#include "../protocol/protocol_defs.h" // Для MessageType

// Типы запросов, которые Main Thread может отправить Sender Thread
typedef enum {
    UVM_REQ_NONE,
    // Запрос-ответ
    UVM_REQ_INIT_CHANNEL,
    UVM_REQ_PROVESTI_KONTROL,
    UVM_REQ_VYDAT_REZULTATY,
    UVM_REQ_VYDAT_SOSTOYANIE,
    // Только отправка
    UVM_REQ_PRIYAT_PARAM_SO,
    UVM_REQ_PRIYAT_TIME_REF,
    UVM_REQ_PRIYAT_REPER,
    UVM_REQ_PRIYAT_PARAM_SDR,
    UVM_REQ_PRIYAT_PARAM_3TSO,
    UVM_REQ_PRIYAT_REF_AZIMUTH,
    UVM_REQ_PRIYAT_PARAM_TSD,
    UVM_REQ_PRIYAT_NAV_DANNYE,
    // Сигнал завершения (опционально)
    UVM_REQ_SHUTDOWN
} UvmRequestType;

typedef struct {
    UvmRequestType type;
    uint8_t tk_param;
    uint8_t vpk_param;
    // ... другие параметры ...
} UvmRequest;

/**
 * @brief Возвращает строковое представление имени сообщения протокола,
 *        которое соответствует данному типу запроса UVM.
 * @param req_type Тип запроса UVM.
 * @return Строка с именем сообщения или "Неизвестный запрос".
 */
const char* uvm_request_type_to_message_name(UvmRequestType req_type); // <-- Добавлен прототип

#endif // UVM_TYPES_H