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
#include "../io/io_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <arpa/inet.h> // <-- ДОБАВЛЕНО для ntohs/ntohl (если нужны в printf)

// --- Глобальные переменные ---
SVMState currentSvmState = STATE_NOT_INITIALIZED;
uint16_t currentMessageCounter = 0;

// --- Определение массива указателей ---
MessageHandler message_handlers[256];

// --- Реализации обработчиков ---

// [4.2.1] «Инициализация канала»
void handle_init_channel_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	printf("Получено сообщение инициализации канала\n");
    uint16_t receivedMessageNumber = get_full_message_number(&receivedMessage->header);
    printf("Номер полученного сообщения: %u\n", receivedMessageNumber);

	InitChannelBody *body = (InitChannelBody *)receivedMessage->body;
	printf("Получено сообщение инициализации канала от UVM: LAUVM=0x%02X, LAK=0x%02X\n", body->lauvm, body->lak);
	printf("SVM: Эмуляция выключения лазера в неиспользуемом канале...\n");

	uint8_t slp = 0x03;
	uint8_t vdr = 0x10;
	uint8_t bop1 = 0x11;
	uint8_t bop2 = 0x12;

	Message confirmMessage = create_confirm_init_message(
                                body->lak,
                                slp, vdr, bop1, bop2,
                                bcbCounter,
                                currentMessageCounter++
                                );
	if (send_protocol_message(io, clientSocketFD, &confirmMessage) != 0) {
		 fprintf(stderr, "SVM: Ошибка отправки подтверждения инициализации\n");
	} else {
	    printf("Отправлено сообщение подтверждения инициализации\n");
    }
	currentSvmState = STATE_INITIALIZED;
}

// [4.2.2] «Подтверждение инициализации канала» (заглушка)
void handle_confirm_init_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
	(void)clientSocketFD;
    (void)receivedMessage;
    printf("SVM получил сообщение Confirm Init (не ожидается)\n");
}

// [4.2.3] «Провести контроль»
void handle_provesti_kontrol_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    printf("Получено сообщение провести контроль\n");
    currentSvmState = STATE_SELF_TEST;
	printf("SVM: Эмуляция самопроверки...\n");
	sleep(1);
	currentSvmState = STATE_INITIALIZED;

    ProvestiKontrolBody *receivedBody = (ProvestiKontrolBody *)receivedMessage->body;

    LogicalAddress svm_lak = LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL; // Используем известный адрес SVM
    // Проверка адреса в заголовке больше не нужна

    Message confirmMessage = create_podtverzhdenie_kontrolya_message(
                                svm_lak,
                                receivedBody->tk,
                                bcbCounter,
                                currentMessageCounter++
                                );
	if (send_protocol_message(io, clientSocketFD, &confirmMessage) != 0) {
		 fprintf(stderr, "SVM: Ошибка отправки подтверждения контроля\n");
	} else {
	    printf("Отправлено сообщение подтверждения контроля\n");
    }
}

// [4.2.4] «Подтверждение контроля» (заглушка)
void handle_podtverzhdenie_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    (void)receivedMessage;
    printf("SVM получил сообщение Podtverzhdenie Kontrolya (не ожидается)\n");
}

// [4.2.5] «Выдать результаты контроля»
void handle_vydat_rezultaty_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	printf("Получено сообщение выдать результаты контроля\n");

	uint8_t rsk = 0x3F;
	uint16_t vsk = 150;
    LogicalAddress svm_lak = LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL; // Используем известный адрес SVM

	Message resultsMessage = create_rezultaty_kontrolya_message(
                                svm_lak,
                                rsk,
                                vsk,
                                bcbCounter,
                                currentMessageCounter++
                                );
	if (send_protocol_message(io, clientSocketFD, &resultsMessage) != 0) {
		fprintf(stderr, "SVM: Ошибка отправки результатов контроля\n");
	} else {
	    printf("Отправлено сообщение с результатами контроля\n");
    }
    (void)receivedMessage; // Подавляем предупреждение
}

// [4.2.6] «Результаты контроля» (заглушка)
void handle_rezultaty_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    (void)receivedMessage;
    printf("SVM получил сообщение Rezultaty Kontrolya (не ожидается)\n");
}

// [4.2.7] «Выдать состояние линии»
void handle_vydat_sostoyanie_linii_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	printf("Получено сообщение выдать состояние линии\n");

	print_counters();

    LogicalAddress svm_lak = LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL; // Используем известный адрес SVM

	Message sostoyanieMessage = create_sostoyanie_linii_message(
                                svm_lak,
                                linkUpChangesCounter,
                                linkUpLowTimeSeconds,
                                signDetChangesCounter,
                                bcbCounter,
                                currentMessageCounter++
                                );
	if (send_protocol_message(io, clientSocketFD, &sostoyanieMessage) != 0) {
		fprintf(stderr, "SVM: Ошибка отправки состояния линии\n");
	} else {
	    printf("Отправлено сообщение состояния линии\n");
    }
    (void)receivedMessage; // Подавляем предупреждение
}

// [4.2.8] «Состояние линии» (заглушка)
void handle_sostoyanie_linii_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io;
    (void)clientSocketFD;
    (void)receivedMessage;
	printf("SVM получил сообщение Sostoyanie Linii (не ожидается)\n");
}

// [4.2.9] «Принять параметры СО» (заглушка)
void handle_prinyat_parametry_so_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    (void)receivedMessage;
    printf("SVM получил сообщение 'Принять параметры СО' (заглушка)\n");
}

// [4.2.10] «Принять TIME_REF_RANGE» (заглушка)
void handle_prinyat_time_ref_range_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    printf("SVM получил сообщение 'Принять TIME_REF_RANGE' (заглушка)\n");
	PrinyatTimeRefRangeBody *body = (PrinyatTimeRefRangeBody*)receivedMessage->body;
	// Данные уже в хост-порядке (int8_t не меняются)
	printf("  time_ref_range[0]: imag=%d, real=%d\n", body->time_ref_range[0].imag, body->time_ref_range[0].real);
}

// [4.2.11] «Принять Reper» (заглушка)
void handle_prinyat_reper_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    printf("SVM получил сообщение 'Принять Reper' (заглушка)\n");
	PrinyatReperBody *body = (PrinyatReperBody*)receivedMessage->body;
	// Данные уже в хост-порядке после receive_protocol_message -> message_to_host_byte_order
	printf("  Reper 1: NTSO=%u, R=%u, A=%u\n", body->NTSO1, body->ReperR1, body->ReperA1); // Убрали ntohs
}

// [4.2.12] «Принять параметры СДР» (заглушка)
void handle_prinyat_parametry_sdr_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
    printf("SVM получил сообщение 'Принять параметры СДР' (заглушка)\n");
    (void)receivedMessage;
}

// [4.2.13] «Принять параметры 3ЦО» (заглушка)
void handle_prinyat_parametry_3tso_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io;
    (void)clientSocketFD;
	printf("SVM получил сообщение 'Принять параметры 3ЦО' (заглушка)\n");
	PrinyatParametry3TsoBody *body = (PrinyatParametry3TsoBody *)receivedMessage->body;
	// Данные уже в хост-порядке
	printf("  Ncadr: %u\n", body->Ncadr); // Убрали ntohs
	printf("  Xnum: %u\n", body->Xnum);
}

// [4.2.14] «Принять REF_AZIMUTH» (заглушка)
void handle_prinyat_ref_azimuth_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
    (void)io;
    (void)clientSocketFD;
	printf("SVM получил сообщение 'Принять REF_AZIMUTH'\n");

	if (receivedMessage->header.body_length != sizeof(PrinyatRefAzimuthBody)) {
		fprintf(stderr, "  Ошибка: Неожиданный размер тела для 'Принять REF_AZIMUTH'. Ожидалось %zu, получено %u\n",
				sizeof(PrinyatRefAzimuthBody), receivedMessage->header.body_length);
		return;
	}

	PrinyatRefAzimuthBody *body = (PrinyatRefAzimuthBody*)receivedMessage->body;
	// Данные уже в хост-порядке
	printf("  NTSO: %u\n", body->NTSO);
	printf("  ref_azimuth[0]: %d\n", body->ref_azimuth[0]);
	printf("  ref_azimuth[%d]: %d\n", REF_AZIMUTH_SIZE - 1, body->ref_azimuth[REF_AZIMUTH_SIZE - 1]);
}

// [4.2.15] «Принять параметры ЦДР» (заглушка)
void handle_prinyat_parametry_tsd_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
	printf("SVM получил сообщение 'Принять параметры ЦДР' (заглушка)\n");
	(void)receivedMessage;
}

// [4.2.16] «Навигационные данные» (заглушка)
void handle_navigatsionnye_dannye_message(IOInterface *io, int clientSocketFD, Message *receivedMessage) {
	(void)io;
    (void)clientSocketFD;
	printf("SVM получил сообщение 'Навигационные данные' (заглушка)\n");
	(void)receivedMessage;
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