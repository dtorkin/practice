/*
 * uvm/uvm_comm.c
 *
 * Описание:
 * Реализация функций для взаимодействия UVM с SVM.
 */

#include "uvm_comm.h"
#include "../io/io_common.h"          // Для send_protocol_message, receive_protocol_message
#include "../protocol/message_builder.h" // Для создания сообщений
#include "../protocol/message_utils.h" // Для get_full_message_number
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // Для sleep
#include <string.h> // Для memcpy

// Убрали DELAY_BETWEEN_MESSAGES_SEC, пусть main решает, нужны ли паузы

// --- Функции "Запрос-Ответ" ---

ConfirmInitBody* send_init_channel_and_receive_confirm(IOInterface *io, uint16_t *messageCounter, Message *receivedMessage) {
    if (!io || io->io_handle < 0) return NULL; // Проверка дескриптора
    Message initChannelMessage = create_init_channel_message(LOGICAL_ADDRESS_UVM_VAL, LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
    if (send_protocol_message(io, io->io_handle, &initChannelMessage) != 0) { // Используем новую функцию
        fprintf(stderr, "UVM COMM: Ошибка отправки сообщения инициализации канала\n"); // Добавил UVM COMM
        return NULL;
    }
    printf("Отправлено сообщение инициализации канала\n");
    // sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку отсюда

    int recvStatus = receive_protocol_message(io, io->io_handle, receivedMessage); // Используем новую функцию
    if (recvStatus != 0) {
        if (recvStatus < 0) fprintf(stderr, "UVM COMM: Ошибка получения подтверждения инициализации.\n");
        return NULL;
    }

    if (receivedMessage->header.message_type != MESSAGE_TYPE_CONFIRM_INIT) {
        fprintf(stderr, "UVM COMM: Полученное сообщение не является подтверждением инициализации. Тип: %u\n", receivedMessage->header.message_type);
        return NULL;
    }

    printf("Получено сообщение подтверждения инициализации\n");
    return (ConfirmInitBody *)receivedMessage->body;
}

PodtverzhdenieKontrolyaBody* send_provesti_kontrol_and_receive_podtverzhdenie(IOInterface *io, uint16_t *messageCounter, Message *receivedMessage, uint8_t tk) {
     if (!io || io->io_handle < 0) return NULL;
    Message provestiKontrolMessage = create_provesti_kontrol_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, tk, (*messageCounter)++);
    if (send_protocol_message(io, io->io_handle, &provestiKontrolMessage) != 0) { // Используем новую функцию
        perror("UVM COMM: Ошибка отправки сообщения провести контроль");
        return NULL;
    }
    printf("Отправлено сообщение провести контроль\n");
    // sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку

    int recvStatus = receive_protocol_message(io, io->io_handle, receivedMessage); // Используем новую функцию
     if (recvStatus != 0) {
        if (recvStatus < 0) fprintf(stderr, "UVM COMM: Ошибка получения подтверждения контроля.\n");
        return NULL;
    }

    if (receivedMessage->header.message_type != MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA) {
        fprintf(stderr, "UVM COMM: Полученное сообщение не является подтверждением контроля. Тип: %u\n", receivedMessage->header.message_type);
        return NULL;
    }

    printf("Получено сообщение подтверждения контроля\n");
    return (PodtverzhdenieKontrolyaBody *)receivedMessage->body;
}


RezultatyKontrolyaBody* send_vydat_rezultaty_kontrolya_and_receive_rezultaty(IOInterface *io, uint16_t *messageCounter, Message *receivedMessage, uint8_t vpk) {
    if (!io || io->io_handle < 0) return NULL;
    Message vydatRezultatyKontrolyaMessage = create_vydat_rezultaty_kontrolya_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, vpk, (*messageCounter)++);
    if (send_protocol_message(io, io->io_handle, &vydatRezultatyKontrolyaMessage) != 0) { // Используем новую функцию
        perror("UVM COMM: Ошибка отправки сообщения выдать результаты");
        return NULL;
    }
    printf("Отправлено сообщение выдать результаты контроля\n");
    // sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку

    int recvStatus = receive_protocol_message(io, io->io_handle, receivedMessage); // Используем новую функцию
    if (recvStatus != 0) {
        if (recvStatus < 0) fprintf(stderr, "UVM COMM: Ошибка получения результатов контроля.\n");
        return NULL;
    }

    if (receivedMessage->header.message_type != MESSAGE_TYPE_RESULTATY_KONTROLYA) {
        fprintf(stderr, "UVM COMM: Полученное сообщение не является результатами контроля. Тип: %u\n", receivedMessage->header.message_type);
        return NULL;
    }

    printf("Получено сообщение с результатами контроля\n");
    return (RezultatyKontrolyaBody *)receivedMessage->body;
}


SostoyanieLiniiBody* send_vydat_sostoyanie_linii_and_receive_sostoyanie(IOInterface *io, uint16_t *messageCounter, Message *receivedMessage) {
    if (!io || io->io_handle < 0) return NULL;
    Message vydatSostoyanieLiniiMessage = create_vydat_sostoyanie_linii_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
    if (send_protocol_message(io, io->io_handle, &vydatSostoyanieLiniiMessage) != 0) { // Используем новую функцию
        perror("UVM COMM: Ошибка отправки сообщения выдать состояние");
        return NULL;
    }
    printf("Отправлено сообщение выдать состояние линии\n");
    // sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку

    int recvStatus = receive_protocol_message(io, io->io_handle, receivedMessage); // Используем новую функцию
    if (recvStatus != 0) {
        if (recvStatus < 0) fprintf(stderr, "UVM COMM: Ошибка получения состояния линии.\n");
        return NULL;
    }

    if (receivedMessage->header.message_type != MESSAGE_TYPE_SOSTOYANIE_LINII) {
        fprintf(stderr, "UVM COMM: Полученное сообщение не является состоянием линии. Тип: %u\n", receivedMessage->header.message_type);
        return NULL;
    }

    printf("Получено сообщение состояния линии\n");
    return (SostoyanieLiniiBody *)receivedMessage->body;
}


// --- Функции только для отправки ---

int send_prinyat_parametry_so(IOInterface *io, uint16_t *messageCounter) {
    if (!io || io->io_handle < 0) return -1;
	Message msg = create_prinyat_parametry_so_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_protocol_message(io, io->io_handle, &msg) != 0) { // Используем новую функцию
		fprintf(stderr, "UVM COMM: Ошибка отправки сообщения 'Принять параметры СО'\n"); // Добавил UVM COMM
        return -1;
	}
	printf("Отправлено сообщение 'Принять параметры СО' с тестовыми данными\n");
	printf("Данные тела сообщения 'Принять параметры СО' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	// sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку
    return 0;
}

int send_prinyat_time_ref_range(IOInterface *io, uint16_t *messageCounter) {
    if (!io || io->io_handle < 0) return -1;
	Message msg = create_prinyat_time_ref_range_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_protocol_message(io, io->io_handle, &msg) != 0) { // Используем новую функцию
		fprintf(stderr, "UVM COMM: Ошибка отправки сообщения 'Принять TIME_REF_RANGE'\n");
        return -1;
	}
	printf("Отправлено сообщение 'Принять TIME_REF_RANGE'\n");
	printf("Данные тела сообщения 'Принять TIME_REF_RANGE' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	// sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку
    return 0;
}

int send_prinyat_reper(IOInterface *io, uint16_t *messageCounter) {
    if (!io || io->io_handle < 0) return -1;
	Message msg = create_prinyat_reper_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_protocol_message(io, io->io_handle, &msg) != 0) { // Используем новую функцию
		fprintf(stderr, "UVM COMM: Ошибка отправки сообщения 'Принять Reper'\n");
        return -1;
	}
	printf("Отправлено сообщение 'Принять Reper'\n");
	printf("Данные тела сообщения 'Принять Reper': ");
	for (int i = 0; i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("\n");
	// sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку
    return 0;
}

int send_prinyat_parametry_sdr(IOInterface *io, uint16_t *messageCounter) {
     if (!io || io->io_handle < 0) return -1;
    // ВАЖНО: create_prinyat_parametry_sdr_message возвращает только базовую часть.
    Message msg = create_prinyat_parametry_sdr_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
    // TODO: Добавить логику добавления массива HRR и обновления длины тела перед отправкой
    // ...

	if (send_protocol_message(io, io->io_handle, &msg) != 0) { // Используем новую функцию
		fprintf(stderr, "UVM COMM: Ошибка отправки сообщения 'Принять параметры СДР'\n");
        return -1;
	}
	printf("Отправлено сообщение 'Принять параметры СДР' c тестовыми данными (только базовая часть)\n");
	printf("Данные тела сообщения 'Принять параметры СДР' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	// sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку
    return 0;
}

int send_prinyat_parametry_3tso(IOInterface *io, uint16_t *messageCounter) {
     if (!io || io->io_handle < 0) return -1;
	Message msg = create_prinyat_parametry_3tso_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_protocol_message(io, io->io_handle, &msg) != 0) { // Используем новую функцию
		fprintf(stderr, "UVM COMM: Ошибка отправки сообщения 'Принять параметры 3ЦО'\n");
        return -1;
	}
	printf("Отправлено сообщение 'Принять параметры 3ЦО' с тестовыми данными\n");
	printf("Данные тела сообщения 'Принять параметры 3ЦО' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	// sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку
    return 0;
}

int send_prinyat_ref_azimuth(IOInterface *io, uint16_t *messageCounter) {
     if (!io || io->io_handle < 0) return -1;
	Message msg = create_prinyat_ref_azimuth_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_protocol_message(io, io->io_handle, &msg) != 0) { // Используем новую функцию
		fprintf(stderr, "UVM COMM: Ошибка отправки сообщения 'Принять REF_AZIMUTH'\n");
        return -1;
	}
	printf("Отправлено сообщение 'Принять REF_AZIMUTH' (длина тела: %d байт)\n", msg.header.body_length);
	printf("Данные тела сообщения 'Принять REF_AZIMUTH' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	// sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку
    return 0;
}

int send_prinyat_parametry_tsd(IOInterface *io, uint16_t *messageCounter) {
    if (!io || io->io_handle < 0) return -1;
    // ВАЖНО: create_prinyat_parametry_tsd_message возвращает только базовую часть.
    Message msg = create_prinyat_parametry_tsd_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
    // TODO: Логика добавления массивов OKM, HShMR, HAR и обновления длины тела...

	if (send_protocol_message(io, io->io_handle, &msg) != 0) { // Используем новую функцию
		fprintf(stderr, "UVM COMM: Ошибка отправки сообщения 'Принять параметры ЦДР'\n");
        return -1;
	}
	printf("Отправлено сообщение 'Принять параметры ЦДР' с тестовыми данными (только базовая часть)\n");
	printf("Данные тела сообщения 'Принять параметры ЦДР' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	// sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку
    return 0;
}

int send_navigatsionnye_dannye(IOInterface *io, uint16_t *messageCounter) {
    if (!io || io->io_handle < 0) return -1;
	Message msg = create_navigatsionnye_dannye_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_protocol_message(io, io->io_handle, &msg) != 0) { // Используем новую функцию
		fprintf(stderr, "UVM COMM: Ошибка отправки сообщения 'Навигационные данные'\n");
        return -1;
	}
	printf("Отправлено сообщение 'Навигационные данные'\n");
	printf("Данные тела сообщения 'Навигационные данные' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	// sleep(DELAY_BETWEEN_MESSAGES_SEC); // Убрали задержку
    return 0;
}