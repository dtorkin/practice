/*
 * svm/svm_handlers.c
 *
 * Описание:
 * Реализации функций-обработчиков для входящих сообщений SVM
 * и инициализация диспетчера сообщений.
 */

#include "svm_handlers.h"
#include "svm_timers.h"
#include "../protocol/message_builder.h"
#include "../protocol/message_utils.h"
#include "../io/io_common.h" // Больше не нужен для send_protocol_message
#include <stdio.h>
#include <stdlib.h> // Для malloc, free
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>

// --- Глобальные переменные ---
SVMState currentSvmState = STATE_NOT_INITIALIZED;
uint16_t currentMessageCounter = 0; // Счетчик исходящих сообщений SVM

// --- Определение массива указателей ---
MessageHandler message_handlers[256];

// --- Реализации обработчиков ---

// [4.2.1] «Инициализация канала» -> Отправляет [4.2.2] «Подтверждение инициализации»
Message* handle_init_channel_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io; // io и clientSocketFD больше не нужны для отправки здесь
    (void)clientSocketFD;

	printf("Processor: Обработка 'Инициализация канала'\n");
    uint16_t receivedMessageNumber = get_full_message_number(&receivedMessage->header);
    printf("  Номер полученного сообщения: %u\n", receivedMessageNumber);

	InitChannelBody *req_body = (InitChannelBody *)receivedMessage->body;
	printf("  Параметры: LAUVM=0x%02X, LAK=0x%02X\n", req_body->lauvm, req_body->lak);
	printf("  SVM: Эмуляция выключения лазера...\n");

	uint8_t slp = 0x03;
	uint8_t vdr = 0x10;
	uint8_t bop1 = 0x11;
	uint8_t bop2 = 0x12;
	uint32_t current_bcb = get_bcb_counter();

    // Создаем ответное сообщение в динамической памяти
    Message *responseMessage = (Message*)malloc(sizeof(Message));
    if (!responseMessage) {
        perror("handle_init_channel: Failed to allocate memory for response");
        return NULL;
    }

	*responseMessage = create_confirm_init_message(
                                req_body->lak, // Используем запрошенный LAK
                                slp, vdr, bop1, bop2,
                                current_bcb,
                                currentMessageCounter++
                                );

	printf("  Ответ 'Подтверждение инициализации' сформирован.\n");
	currentSvmState = STATE_INITIALIZED;
    return responseMessage; // Возвращаем указатель на ответ
}

// [4.2.2] «Подтверждение инициализации канала» (заглушка, нет ответа)
Message* handle_confirm_init_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
	(void)clientSocketFD;
    (void)receivedMessage;
    printf("Processor: Обработка 'Подтверждение инициализации' (не ожидается) - ответа нет.\n");
    return NULL; // Нет ответа
}

// [4.2.3] «Провести контроль» -> Отправляет [4.2.4] «Подтверждение контроля»
Message* handle_provesti_kontrol_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io;
    (void)clientSocketFD;
    printf("Processor: Обработка 'Провести контроль'\n");

    // Эмулируем контроль
    // pthread_mutex_lock(&svm_state_mutex);
    currentSvmState = STATE_SELF_TEST;
    // pthread_mutex_unlock(&svm_state_mutex);
	printf("  SVM: Эмуляция самопроверки...\n");
	sleep(1);
    // pthread_mutex_lock(&svm_state_mutex);
	currentSvmState = STATE_INITIALIZED;
    // pthread_mutex_unlock(&svm_state_mutex);

    ProvestiKontrolBody *req_body = (ProvestiKontrolBody *)receivedMessage->body;
    LogicalAddress svm_lak = LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL; // Адрес этого SVM
    uint32_t current_bcb = get_bcb_counter();

    // Создаем ответ
    Message *responseMessage = (Message*)malloc(sizeof(Message));
     if (!responseMessage) {
        perror("handle_provesti_kontrol: Failed to allocate memory for response");
        return NULL;
    }
    *responseMessage = create_podtverzhdenie_kontrolya_message(
                                svm_lak,
                                req_body->tk, // Возвращаем тип контроля из запроса
                                current_bcb,
                                currentMessageCounter++
                                );

	printf("  Ответ 'Подтверждение контроля' сформирован.\n");
    return responseMessage;
}

// [4.2.4] «Подтверждение контроля» (заглушка, нет ответа)
Message* handle_podtverzhdenie_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    (void)receivedMessage;
    printf("Processor: Обработка 'Подтверждение контроля' (не ожидается) - ответа нет.\n");
    return NULL;
}

// [4.2.5] «Выдать результаты контроля» -> Отправляет [4.2.6] «Результаты контроля»
Message* handle_vydat_rezultaty_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io;
    (void)clientSocketFD;
    printf("Processor: Обработка 'Выдать результаты контроля'\n");

	uint8_t rsk = 0x3F; // Все ОК
	uint16_t vsk = 150; // 150 мс
    LogicalAddress svm_lak = LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL;
    uint32_t current_bcb = get_bcb_counter();

    // Создаем ответ
     Message *responseMessage = (Message*)malloc(sizeof(Message));
     if (!responseMessage) {
        perror("handle_vydat_rezultaty: Failed to allocate memory for response");
        return NULL;
    }
	*responseMessage = create_rezultaty_kontrolya_message(
                                svm_lak,
                                rsk,
                                vsk,
                                current_bcb,
                                currentMessageCounter++
                                );

	printf("  Ответ 'Результаты контроля' сформирован.\n");
    (void)receivedMessage;
    return responseMessage;
}

// [4.2.6] «Результаты контроля» (заглушка, нет ответа)
Message* handle_rezultaty_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    (void)receivedMessage;
    printf("Processor: Обработка 'Результаты контроля' (не ожидается) - ответа нет.\n");
    return NULL;
}

// [4.2.7] «Выдать состояние линии» -> Отправляет [4.2.8] «Состояние линии»
Message* handle_vydat_sostoyanie_linii_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io;
    (void)clientSocketFD;
    (void)receivedMessage;
	printf("Processor: Обработка 'Выдать состояние линии'\n");

	print_counters(); // Выводим текущие значения

    LogicalAddress svm_lak = LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL;
    uint16_t kla_val;
    uint32_t sla_val;
    uint16_t ksa_val;
    uint32_t current_bcb = get_bcb_counter();
    get_line_status_counters(&kla_val, &sla_val, &ksa_val);

    // Создаем ответ
    Message *responseMessage = (Message*)malloc(sizeof(Message));
     if (!responseMessage) {
        perror("handle_vydat_sostoyanie: Failed to allocate memory for response");
        return NULL;
    }
	*responseMessage = create_sostoyanie_linii_message(
                                svm_lak,
                                kla_val,
                                sla_val,
                                ksa_val,
                                current_bcb,
                                currentMessageCounter++
                                );
	printf("  Ответ 'Состояние линии' сформирован.\n");
    return responseMessage;
}

// [4.2.8] «Состояние линии» (заглушка, нет ответа)
Message* handle_sostoyanie_linii_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io;
    (void)clientSocketFD;
    (void)receivedMessage;
	printf("Processor: Обработка 'Состояние линии' (не ожидается) - ответа нет.\n");
    return NULL;
}

// [4.2.9] «Принять параметры СО» (нет ответа)
Message* handle_prinyat_parametry_so_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    printf("Processor: Обработка 'Принять параметры СО'.\n");
    // Здесь будет логика сохранения параметров из receivedMessage->body
    (void)receivedMessage;
    return NULL; // Нет ответного сообщения по протоколу
}

// [4.2.10] «Принять TIME_REF_RANGE» (нет ответа)
Message* handle_prinyat_time_ref_range_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    printf("Processor: Обработка 'Принять TIME_REF_RANGE'.\n");
	PrinyatTimeRefRangeBody *body = (PrinyatTimeRefRangeBody*)receivedMessage->body;
	printf("  time_ref_range[0]: imag=%d, real=%d\n", body->time_ref_range[0].imag, body->time_ref_range[0].real);
    // Логика сохранения массива
     return NULL;
}

// [4.2.11] «Принять Reper» (нет ответа)
Message* handle_prinyat_reper_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    printf("Processor: Обработка 'Принять Reper'.\n");
	PrinyatReperBody *body = (PrinyatReperBody*)receivedMessage->body;
	printf("  Reper 1: NTSO=%u, R=%u, A=%u\n", body->NTSO1, body->ReperR1, body->ReperA1);
    // Логика сохранения
     return NULL;
}

// [4.2.12] «Принять параметры СДР» (нет ответа)
Message* handle_prinyat_parametry_sdr_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    printf("Processor: Обработка 'Принять параметры СДР'.\n");
    // Логика обработки базовой части и массива HRR
    (void)receivedMessage;
     return NULL;
}

// [4.2.13] «Принять параметры 3ЦО» (нет ответа)
Message* handle_prinyat_parametry_3tso_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io;
    (void)clientSocketFD;
	printf("Processor: Обработка 'Принять параметры 3ЦО'.\n");
	PrinyatParametry3TsoBody *body = (PrinyatParametry3TsoBody *)receivedMessage->body;
	printf("  Ncadr: %u\n", body->Ncadr);
	printf("  Xnum: %u\n", body->Xnum);
    // Логика сохранения
     return NULL;
}

// [4.2.14] «Принять REF_AZIMUTH» (нет ответа)
Message* handle_prinyat_ref_azimuth_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io;
    (void)clientSocketFD;
	printf("Processor: Обработка 'Принять REF_AZIMUTH'.\n");

	if (receivedMessage->header.body_length != sizeof(PrinyatRefAzimuthBody)) {
		fprintf(stderr, "  Ошибка: Некорректный размер тела для 'Принять REF_AZIMUTH'.\n");
		return NULL; // Не можем обработать
	}

	PrinyatRefAzimuthBody *body = (PrinyatRefAzimuthBody*)receivedMessage->body;
	printf("  NTSO: %u\n", body->NTSO);
	printf("  ref_azimuth[0]: %d\n", body->ref_azimuth[0]);
	printf("  ref_azimuth[%d]: %d\n", REF_AZIMUTH_SIZE - 1, body->ref_azimuth[REF_AZIMUTH_SIZE - 1]);
    // Логика сохранения
    return NULL;
}

// [4.2.15] «Принять параметры ЦДР» (нет ответа)
Message* handle_prinyat_parametry_tsd_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
	printf("Processor: Обработка 'Принять параметры ЦДР'.\n");
    // Логика обработки базовой части и массивов OKM, HShMR, HAR
	(void)receivedMessage;
     return NULL;
}

// [4.2.16] «Навигационные данные» (нет ответа)
Message* handle_navigatsionnye_dannye_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
	printf("Processor: Обработка 'Навигационные данные'.\n");
    // Логика сохранения массива mnd
	(void)receivedMessage;
     return NULL;
}

// --- Инициализация диспетчера ---
void init_message_handlers(void) {
	for (int i = 0; i < 256; ++i) {
		message_handlers[i] = NULL;
	}
	// УВМ -> СВМ
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

    // СВМ -> УВМ (Заглушки)
	message_handlers[MESSAGE_TYPE_CONFIRM_INIT] = handle_confirm_init_message;
	message_handlers[MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA] = handle_podtverzhdenie_kontrolya_message;
	message_handlers[MESSAGE_TYPE_RESULTATY_KONTROLYA] = handle_rezultaty_kontrolya_message;
	message_handlers[MESSAGE_TYPE_SOSTOYANIE_LINII] = handle_sostoyanie_linii_message;
    // ... Добавить остальные заглушки ...
}