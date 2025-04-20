/*
 * svm/svm_handlers.c
 * Описание: Реализации функций-обработчиков для входящих сообщений SVM.
 * (Возвращено к одно-экземплярной модели с глобальными переменными)
 */
#include "svm_handlers.h"
#include "svm_timers.h" // Для доступа к глобальным счетчикам
#include "../protocol/message_builder.h"
#include "../protocol/message_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h> // Для ntohs

// --- Глобальные переменные (снова используются) ---
SVMState currentSvmState = STATE_NOT_INITIALIZED;
uint16_t currentMessageCounter = 0; // Счетчик исходящих сообщений SVM
LogicalAddress svm_logical_address = LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL; // Адрес этого SVM (будет переопределен из конфига)

// Массив указателей
MessageHandler message_handlers[256];

// --- Реализации обработчиков ---

Message* handle_init_channel_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io; (void)clientSocketFD; // Не используются здесь напрямую
    printf("Processor: Обработка 'Инициализация канала'\n");
    InitChannelBody *req_body = (InitChannelBody *)receivedMessage->body;

    // Сохраняем запрошенный LAK как наш адрес (важно для параметризации)
    svm_logical_address = req_body->lak;
    printf("  SVM LAK set to 0x%02X from request.\n", svm_logical_address);

    printf("  SVM: Эмуляция выключения лазера...\n");
    uint8_t slp = 0x03, vdr = 0x10, bop1 = 0x11, bop2 = 0x12;
    uint32_t current_bcb = get_bcb_counter(); // Глобальный счетчик

    Message *responseMessage = (Message*)malloc(sizeof(Message));
    if (!responseMessage) { /*...*/ return NULL; }

    *responseMessage = create_confirm_init_message(
                                svm_logical_address, // Отвечаем с нашим (установленным) LAK
                                slp, vdr, bop1, bop2, current_bcb,
                                currentMessageCounter++); // Глобальный счетчик сообщений

    printf("  Ответ 'Подтверждение инициализации' сформирован (LAK=0x%02X).\n", svm_logical_address);
    currentSvmState = STATE_INITIALIZED; // Глобальное состояние
    return responseMessage;
}

Message* handle_provesti_kontrol_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io; (void)clientSocketFD;
    printf("Processor: Обработка 'Провести контроль'\n");

    currentSvmState = STATE_SELF_TEST;
    printf("  SVM: Эмуляция самопроверки...\n");
    sleep(1);
    currentSvmState = STATE_INITIALIZED;

    ProvestiKontrolBody *req_body = (ProvestiKontrolBody *)receivedMessage->body;
    uint32_t current_bcb = get_bcb_counter();

    Message *responseMessage = (Message*)malloc(sizeof(Message));
    if (!responseMessage) { /*...*/ return NULL; }
    *responseMessage = create_podtverzhdenie_kontrolya_message(
                                svm_logical_address, req_body->tk, current_bcb,
                                currentMessageCounter++);
    printf("  Ответ 'Подтверждение контроля' сформирован.\n");
    return responseMessage;
}

Message* handle_vydat_rezultaty_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io; (void)clientSocketFD; (void)receivedMessage;
    printf("Processor: Обработка 'Выдать результаты контроля'\n");

    // Имитация разного результата для разных LAK (если нужно)
    uint8_t rsk = 0x3F; // По умолчанию ОК
    if (svm_logical_address == 0x09) { // Пример: SVM с LAK=0x09 сообщает об ошибке
         rsk = 0x3E;
         printf("  SVM (LAK 0x%02X): Emulating control failure (RSK=0x%02X).\n", svm_logical_address, rsk);
    }
    uint16_t vsk = 150;
    uint32_t current_bcb = get_bcb_counter();

    Message *responseMessage = (Message*)malloc(sizeof(Message));
    if (!responseMessage) { /*...*/ return NULL; }
    *responseMessage = create_rezultaty_kontrolya_message(
                                svm_logical_address, rsk, vsk, current_bcb,
                                currentMessageCounter++);
    printf("  Ответ 'Результаты контроля' сформирован.\n");
    return responseMessage;
}

 Message* handle_vydat_sostoyanie_linii_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io; (void)clientSocketFD; (void)receivedMessage;
    printf("Processor: Обработка 'Выдать состояние линии'\n");
    print_counters(); // Выводим глобальные счетчики

    uint16_t kla_val;
    uint32_t sla_val_us100;
    uint16_t ksa_val;
    uint32_t current_bcb = get_bcb_counter();
    get_line_status_counters(&kla_val, &sla_val_us100, &ksa_val); // Глобальные счетчики

    Message *responseMessage = (Message*)malloc(sizeof(Message));
    if (!responseMessage) { /*...*/ return NULL; }
    *responseMessage = create_sostoyanie_linii_message(
                                svm_logical_address, kla_val, sla_val_us100, ksa_val, current_bcb,
                                currentMessageCounter++);
    printf("  Ответ 'Состояние линии' сформирован.\n");
    return responseMessage;
}

// --- Заглушки и обработчики без ответа ---
// (Сигнатуры изменены, убраны SvmInstance, используют глобальные переменные если нужно)

Message* handle_confirm_init_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Подтверждение инициализации' (не ожидается) - ответа нет.\n"); return NULL; }
Message* handle_podtverzhdenie_kontrolya_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Подтверждение контроля' (не ожидается) - ответа нет.\n"); return NULL; }
Message* handle_rezultaty_kontrolya_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Результаты контроля' (не ожидается) - ответа нет.\n"); return NULL; }
Message* handle_sostoyanie_linii_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Состояние линии' (не ожидается) - ответа нет.\n"); return NULL; }
Message* handle_prinyat_parametry_so_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Принять параметры СО' (нет ответа).\n"); return NULL; }
Message* handle_prinyat_time_ref_range_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Принять TIME_REF_RANGE' (нет ответа).\n"); return NULL; }
Message* handle_prinyat_reper_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Принять Reper' (нет ответа).\n"); return NULL; }
Message* handle_prinyat_parametry_sdr_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Принять параметры СДР' (нет ответа).\n"); return NULL; }
Message* handle_prinyat_parametry_3tso_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Принять параметры 3ЦО' (нет ответа).\n"); return NULL; }
Message* handle_prinyat_ref_azimuth_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Принять REF_AZIMUTH' (нет ответа).\n"); return NULL; }
Message* handle_prinyat_parametry_tsd_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Принять параметры ЦДР' (нет ответа).\n"); return NULL; }
Message* handle_navigatsionnye_dannye_message(IOInterface *io, int csFD, Message *msg) { (void)io; (void)csFD; (void)msg; printf("Processor: Обработка 'Навигационные данные' (нет ответа).\n"); return NULL; }

// --- Инициализация диспетчера ---
void init_message_handlers(void) {
    for (int i = 0; i < 256; ++i) message_handlers[i] = NULL;
    message_handlers[MESSAGE_TYPE_INIT_CHANNEL] = handle_init_channel_message;
    message_handlers[MESSAGE_TYPE_PROVESTI_KONTROL] = handle_provesti_kontrol_message;
    message_handlers[MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA] = handle_vydat_rezultaty_kontrolya_message;
    message_handlers[MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII] = handle_vydat_sostoyanie_linii_message;
    message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_SO] = handle_prinyat_parametry_so_message;
    message_handlers[MESSAGE_TYPE_PRIYAT_TIME_REF_RANGE] = handle_prinyat_time_ref_range_message;
    message_handlers[MESSAGE_TYPE_PRIYAT_REPER] = handle_prinyat_reper_message;
    message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR] = handle_prinyat_parametry_sdr_message;
    message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO] = handle_prinyat_parametry_3tso_message;
    message_handlers[MESSAGE_TYPE_PRIYAT_REF_AZIMUTH] = handle_prinyat_ref_azimuth_message;
    message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD] = handle_prinyat_parametry_tsd_message;
    message_handlers[MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE] = handle_navigatsionnye_dannye_message;
    // Заглушки
    message_handlers[MESSAGE_TYPE_CONFIRM_INIT] = handle_confirm_init_message;
    message_handlers[MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA] = handle_podtverzhdenie_kontrolya_message;
    message_handlers[MESSAGE_TYPE_RESULTATY_KONTROLYA] = handle_rezultaty_kontrolya_message;
    message_handlers[MESSAGE_TYPE_SOSTOYANIE_LINII] = handle_sostoyanie_linii_message;
    printf("Message handlers initialized.\n");
}