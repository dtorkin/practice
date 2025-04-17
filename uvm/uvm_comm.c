/*
 * uvm/uvm_comm.c
 *
 * Описание:
 * Реализация функций для взаимодействия UVM с SVM.
 */

#include "uvm_comm.h"
#include "../io/io_common.h"          // Для send_message, receive_full_message
#include "../protocol/message_builder.h" // Для создания сообщений
#include "../protocol/message_utils.h" // Для get_full_message_number
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h> // Для sleep
#include <string.h> // Для memcpy при обработке SDR/TSD

// Константы (можно вынести в config или uvm_comm.h)
#define DELAY_BETWEEN_MESSAGES_SEC 1 // Уменьшим для скорости тестов

// --- Функции "Запрос-Ответ" ---

ConfirmInitBody* send_init_channel_and_receive_confirm(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage) {
    Message initChannelMessage = create_init_channel_message(LOGICAL_ADDRESS_UVM_VAL, LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
    if (send_message(clientSocketFD, &initChannelMessage) != 0) {
        perror("UVM COMM: Ошибка отправки сообщения инициализации канала");
        return NULL;
    }
    printf("Отправлено сообщение инициализации канала\n");
    sleep(DELAY_BETWEEN_MESSAGES_SEC);

    int recvStatus = receive_full_message(clientSocketFD, receivedMessage);
    if (recvStatus != 0) {
        if (recvStatus < 0) fprintf(stderr, "UVM COMM: Ошибка получения подтверждения инициализации.\n");
        // Если recvStatus == 1, сообщение уже выведено в receive_full_message
        return NULL;
    }

    if (receivedMessage->header.message_type != MESSAGE_TYPE_CONFIRM_INIT) {
        fprintf(stderr, "UVM COMM: Полученное сообщение не является подтверждением инициализации. Тип: %u\n", receivedMessage->header.message_type);
        return NULL;
    }

    printf("Получено сообщение подтверждения инициализации\n");
    return (ConfirmInitBody *)receivedMessage->body;
}

PodtverzhdenieKontrolyaBody* send_provesti_kontrol_and_receive_podtverzhdenie(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage, uint8_t tk) {
    Message provestiKontrolMessage = create_provesti_kontrol_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, tk, (*messageCounter)++);
    if (send_message(clientSocketFD, &provestiKontrolMessage) != 0) {
        perror("UVM COMM: Ошибка отправки сообщения провести контроль");
        return NULL;
    }
    printf("Отправлено сообщение провести контроль\n");
    sleep(DELAY_BETWEEN_MESSAGES_SEC);

    int recvStatus = receive_full_message(clientSocketFD, receivedMessage);
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


RezultatyKontrolyaBody* send_vydat_rezultaty_kontrolya_and_receive_rezultaty(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage, uint8_t vpk) {
    Message vydatRezultatyKontrolyaMessage = create_vydat_rezultaty_kontrolya_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, vpk, (*messageCounter)++);
    if (send_message(clientSocketFD, &vydatRezultatyKontrolyaMessage) != 0) {
        perror("UVM COMM: Ошибка отправки сообщения выдать результаты");
        return NULL;
    }
    printf("Отправлено сообщение выдать результаты контроля\n");
    sleep(DELAY_BETWEEN_MESSAGES_SEC);

    int recvStatus = receive_full_message(clientSocketFD, receivedMessage);
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


SostoyanieLiniiBody* send_vydat_sostoyanie_linii_and_receive_sostoyanie(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage) {
    Message vydatSostoyanieLiniiMessage = create_vydat_sostoyanie_linii_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
    if (send_message(clientSocketFD, &vydatSostoyanieLiniiMessage) != 0) {
        perror("UVM COMM: Ошибка отправки сообщения выдать состояние");
        return NULL;
    }
    printf("Отправлено сообщение выдать состояние линии\n");
    sleep(DELAY_BETWEEN_MESSAGES_SEC);

    int recvStatus = receive_full_message(clientSocketFD, receivedMessage);
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

int send_prinyat_parametry_so(int clientSocketFD, uint16_t *messageCounter) {
	Message msg = create_prinyat_parametry_so_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_message(clientSocketFD, &msg) != 0) {
		perror("UVM COMM: Ошибка отправки сообщения 'Принять параметры СО'");
        return -1;
	}
	printf("Отправлено сообщение 'Принять параметры СО' с тестовыми данными\n");
	printf("Данные тела сообщения 'Принять параметры СО' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) { // Используем msg.header.body_length (уже в host order)
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
    return 0;
}

int send_prinyat_time_ref_range(int clientSocketFD, uint16_t *messageCounter) {
	Message msg = create_prinyat_time_ref_range_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_message(clientSocketFD, &msg) != 0) {
		perror("UVM COMM: Ошибка отправки сообщения 'Принять TIME_REF_RANGE'");
        return -1;
	}
	printf("Отправлено сообщение 'Принять TIME_REF_RANGE'\n");
	printf("Данные тела сообщения 'Принять TIME_REF_RANGE' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
    return 0;
}

int send_prinyat_reper(int clientSocketFD, uint16_t *messageCounter) {
	Message msg = create_prinyat_reper_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_message(clientSocketFD, &msg) != 0) {
		perror("UVM COMM: Ошибка отправки сообщения 'Принять Reper'");
        return -1;
	}
	printf("Отправлено сообщение 'Принять Reper'\n");
	printf("Данные тела сообщения 'Принять Reper': ");
	for (int i = 0; i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
    return 0;
}

int send_prinyat_parametry_sdr(int clientSocketFD, uint16_t *messageCounter) {
	// ВАЖНО: create_prinyat_parametry_sdr_message возвращает только базовую часть.
    // Здесь нужно добавить логику для добавления массива HRR, если он нужен.
    // Пока отправляем только базовую часть для простоты.
    Message msg = create_prinyat_parametry_sdr_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
    // uint16_t mrr = ((PrinyatParametrySdrBodyBase*)msg.body)->mrr; // Получаем MRR (уже в host order)
    // complex_fixed16_t hrr_test_data[mrr]; // Пример: создаем тестовые данные
    // for(uint16_t i=0; i<mrr; ++i) { hrr_test_data[i] = ...; }
    // memcpy(msg.body + sizeof(PrinyatParametrySdrBodyBase), hrr_test_data, mrr * sizeof(complex_fixed16_t));
    // msg.header.body_length = htons(sizeof(PrinyatParametrySdrBodyBase) + mrr * sizeof(complex_fixed16_t)); // Обновляем длину

	if (send_message(clientSocketFD, &msg) != 0) {
		perror("UVM COMM: Ошибка отправки сообщения 'Принять параметры СДР'");
        return -1;
	}
	printf("Отправлено сообщение 'Принять параметры СДР' c тестовыми данными (только базовая часть)\n");
	printf("Данные тела сообщения 'Принять параметры СДР' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
    return 0;
}

int send_prinyat_parametry_3tso(int clientSocketFD, uint16_t *messageCounter) {
	Message msg = create_prinyat_parametry_3tso_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_message(clientSocketFD, &msg) != 0) {
		perror("UVM COMM: Ошибка отправки сообщения 'Принять параметры 3ЦО'");
        return -1;
	}
	printf("Отправлено сообщение 'Принять параметры 3ЦО' с тестовыми данными\n");
	printf("Данные тела сообщения 'Принять параметры 3ЦО' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
    return 0;
}

int send_prinyat_ref_azimuth(int clientSocketFD, uint16_t *messageCounter) {
	Message msg = create_prinyat_ref_azimuth_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_message(clientSocketFD, &msg) != 0) {
		perror("UVM COMM: Ошибка отправки сообщения 'Принять REF_AZIMUTH'");
        return -1;
	}
	printf("Отправлено сообщение 'Принять REF_AZIMUTH' (длина тела: %d байт)\n", msg.header.body_length);
	printf("Данные тела сообщения 'Принять REF_AZIMUTH' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
    return 0;
}

int send_prinyat_parametry_tsd(int clientSocketFD, uint16_t *messageCounter) {
    // ВАЖНО: create_prinyat_parametry_tsd_message возвращает только базовую часть.
    // Нужно добавить массивы OKM, HShMR, HAR. Пока отправляем только базу.
	Message msg = create_prinyat_parametry_tsd_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
    // Логика добавления массивов и обновления длины...

	if (send_message(clientSocketFD, &msg) != 0) {
		perror("UVM COMM: Ошибка отправки сообщения 'Принять параметры ЦДР'");
        return -1;
	}
	printf("Отправлено сообщение 'Принять параметры ЦДР' с тестовыми данными (только базовая часть)\n");
	printf("Данные тела сообщения 'Принять параметры ЦДР' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
    return 0;
}

int send_navigatsionnye_dannye(int clientSocketFD, uint16_t *messageCounter) {
	Message msg = create_navigatsionnye_dannye_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, (*messageCounter)++);
	if (send_message(clientSocketFD, &msg) != 0) {
		perror("UVM COMM: Ошибка отправки сообщения 'Навигационные данные'");
        return -1;
	}
	printf("Отправлено сообщение 'Навигационные данные'\n");
	printf("Данные тела сообщения 'Навигационные данные' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < msg.header.body_length; ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
    return 0;
}