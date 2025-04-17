/*
uvm.c:
•	Функции «запрос-ответ» для приёма и отправки сообщений (send_***_and_receive_***)
•	Функции «только для отправки» (send_***)
•	Функция main:
	o	Установка сетевого соединения с СВ-М 
	o	Выбор режима работы (из аргументов argv)
	o	Подготовка к наблюдению (вызовы функций "запрос-ответ")
	o	Подготовка к сеансу съемки (вызовы функций "только для отправки" в зависимости от режима)
*/

#include "errno.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include "io/io_common.h"
#include "protocol/protocol_defs.h"
#include "protocol/message_builder.h" // Нужен для создания сообщений
#include "protocol/message_utils.h" // Нужен для get_full_message_number, message_to_host/network_byte_order


// Константы
#define SVM_IP_ADDRESS "192.168.189.129"
#define PORT_UVM 8080
#define DELAY_BETWEEN_MESSAGES_SEC 2

// --- Вспомогательная функция для чтения сообщения ---
// Читает сообщение из сокета, сначала заголовок, потом тело.
// Возвращает 0 при успехе, -1 при ошибке, 1 при закрытии сокета.


/********************************************************************************/
/*				  ФУНКЦИИ ОТПРАВКИ И ПРИЁМА СООБЩЕНИЙ (send_)				  */
/********************************************************************************/

// Пункт 3.3.3.
// [4.2.1] «Инициализация канала» (отправка сообщения)
// [4.2.2] «Подтверждение инициализации канала» (приём сообщения)
// Теперь возвращает указатель на тело внутри receivedMessage или NULL при ошибке/закрытии
ConfirmInitBody* send_init_channel_and_receive_confirm(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage) {
    Message initChannelMessage = create_init_channel_message(LOGICAL_ADDRESS_UVM, LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, (*messageCounter)++);
    if (send_message(clientSocketFD, &initChannelMessage) != 0) {
        perror("Ошибка отправки сообщения инициализации канала");
        return NULL;
    }
    printf("Отправлено сообщение инициализации канала\n");
    sleep(DELAY_BETWEEN_MESSAGES_SEC); // Оставляем задержку для имитации

    int recvStatus = receive_full_message(clientSocketFD, receivedMessage);
    if (recvStatus != 0) {
        if (recvStatus < 0) fprintf(stderr, "Ошибка получения подтверждения инициализации.\n");
        return NULL; // Ошибка или соединение закрыто
    }

    if (receivedMessage->header.message_type != MESSAGE_TYPE_CONFIRM_INIT) {
        fprintf(stderr, "Полученное сообщение не является подтверждением инициализации. Тип: %u\n", receivedMessage->header.message_type);
        return NULL; // Неверный тип сообщения
    }

    printf("Получено сообщение подтверждения инициализации\n");
    return (ConfirmInitBody *)receivedMessage->body;
}

// Пункт 3.3.5.
// [4.2.3] «Провести контроль» (отправка сообщения)
// [4.2.4] «Подтверждение контроля» (приём сообщения)
// Теперь возвращает указатель на тело внутри receivedMessage или NULL при ошибке/закрытии
PodtverzhdenieKontrolyaBody* send_provesti_kontrol_and_receive_podtverzhdenie(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage) {
    Message provestiKontrolMessage = create_provesti_kontrol_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, 0x00, (*messageCounter)++);
    if (send_message(clientSocketFD, &provestiKontrolMessage) != 0) {
        perror("Ошибка отправки сообщения провести контроль");
        return NULL;
    }
    printf("Отправлено сообщение провести контроль\n");
    sleep(DELAY_BETWEEN_MESSAGES_SEC);

    int recvStatus = receive_full_message(clientSocketFD, receivedMessage);
     if (recvStatus != 0) {
        if (recvStatus < 0) fprintf(stderr, "Ошибка получения подтверждения контроля.\n");
        return NULL;
    }

    if (receivedMessage->header.message_type != MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA) {
        fprintf(stderr, "Полученное сообщение не является подтверждением контроля. Тип: %u\n", receivedMessage->header.message_type);
        return NULL;
    }

    printf("Получено сообщение подтверждения контроля\n");
    return (PodtverzhdenieKontrolyaBody *)receivedMessage->body;
}

// Пункт 3.3.6.
// [4.2.5] «Выдать результаты контроля» (отправка сообщения)
// [4.2.6] «Результаты контроля» (отправка сообщения)
// Теперь возвращает указатель на тело внутри receivedMessage или NULL при ошибке/закрытии
RezultatyKontrolyaBody* send_vydat_rezultaty_kontrolya_and_receive_rezultaty(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage) {
    Message vydatRezultatyKontrolyaMessage = create_vydat_rezultaty_kontrolya_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, 0x00, (*messageCounter)++);
    if (send_message(clientSocketFD, &vydatRezultatyKontrolyaMessage) != 0) {
        perror("Ошибка отправки сообщения выдать результаты");
        return NULL;
    }
    printf("Отправлено сообщение выдать результаты контроля\n");
    sleep(DELAY_BETWEEN_MESSAGES_SEC);

    int recvStatus = receive_full_message(clientSocketFD, receivedMessage);
    if (recvStatus != 0) {
        if (recvStatus < 0) fprintf(stderr, "Ошибка получения результатов контроля.\n");
        return NULL;
    }

    if (receivedMessage->header.message_type != MESSAGE_TYPE_RESULTATY_KONTROLYA) {
        fprintf(stderr, "Полученное сообщение не является результатами контроля. Тип: %u\n", receivedMessage->header.message_type);
        return NULL;
    }

    printf("Получено сообщение с результатами контроля\n");
    return (RezultatyKontrolyaBody *)receivedMessage->body;
}

// Пункт 3.3.8.
// [4.2.7] «Выдать состояние линии» (отправка сообщения)
// [4.2.8] «Состояние линии» (отправка сообщения)
// Теперь возвращает указатель на тело внутри receivedMessage или NULL при ошибке/закрытии
SostoyanieLiniiBody* send_vydat_sostoyanie_linii_and_receive_sostoyanie(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage) {
    Message vydatSostoyanieLiniiMessage = create_vydat_sostoyanie_linii_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, (*messageCounter)++);
    if (send_message(clientSocketFD, &vydatSostoyanieLiniiMessage) != 0) {
        perror("Ошибка отправки сообщения выдать состояние");
        return NULL;
    }
    printf("Отправлено сообщение выдать состояние линии\n");
    sleep(DELAY_BETWEEN_MESSAGES_SEC);

    int recvStatus = receive_full_message(clientSocketFD, receivedMessage);
    if (recvStatus != 0) {
        if (recvStatus < 0) fprintf(stderr, "Ошибка получения состояния линии.\n");
        return NULL;
    }

    if (receivedMessage->header.message_type != MESSAGE_TYPE_SOSTOYANIE_LINII) {
        fprintf(stderr, "Полученное сообщение не является состоянием линии. Тип: %u\n", receivedMessage->header.message_type);
        return NULL;
    }

    printf("Получено сообщение состояния линии\n");
    return (SostoyanieLiniiBody *)receivedMessage->body;
}


// Пункт 3.4.2.1.
// [4.2.9] «Принять параметры СО» (отправка сообщения)
void send_prinyat_parametry_so(int clientSocketFD, uint16_t *messageCounter) {
	Message prinyatParametrySoMessage = create_prinyat_parametry_so_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, (*messageCounter)++);
	if (send_message(clientSocketFD, &prinyatParametrySoMessage) != 0) {
		perror("Ошибка отправки сообщения 'Принять параметры СО'");
	}
	printf("Отправлено сообщение 'Принять параметры СО' с тестовыми данными\n");
	printf("Данные тела сообщения 'Принять параметры СО' (первые 20 байт): "); // Выводим для проверки
	for (int i = 0; i < 20 && i < ntohs(prinyatParametrySoMessage.header.body_length); ++i) {
		printf("%02X ", prinyatParametrySoMessage.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
}

// [4.2.10] «Принять TIME_REF_RANGE» (отправка сообщения)
void send_prinyat_time_ref_range(int clientSocketFD, uint16_t *messageCounter) {
	Message msg = create_prinyat_time_ref_range_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, (*messageCounter)++);
	if (send_message(clientSocketFD, &msg) != 0) {
		perror("Ошибка отправки сообщения 'Принять TIME_REF_RANGE'");
	}
	printf("Отправлено сообщение 'Принять TIME_REF_RANGE'\n");
	printf("Данные тела сообщения 'Принять TIME_REF_RANGE' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < ntohs(msg.header.body_length); ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
}

// [4.2.11] «Принять Reper» (отправка сообщения)
void send_prinyat_reper(int clientSocketFD, uint16_t *messageCounter) {
	Message msg = create_prinyat_reper_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, (*messageCounter)++);
	if (send_message(clientSocketFD, &msg) != 0) {
		perror("Ошибка отправки сообщения 'Принять Reper'");
	}
	printf("Отправлено сообщение 'Принять Reper'\n");
	printf("Данные тела сообщения 'Принять Reper': ");
	for (int i = 0; i < ntohs(msg.header.body_length); ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
}

// Пункт 3.4.3.2.
// [4.2.12] «Принять параметры СДР» (отправка сообщения)
void send_prinyat_parametry_sdr(int clientSocketFD, uint16_t *messageCounter) {
	Message prinyatParametrySdrMessage = create_prinyat_parametry_sdr_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, (*messageCounter)++);
	if (send_message(clientSocketFD, &prinyatParametrySdrMessage) != 0) {
		perror("Ошибка отправки сообщения 'Принять параметры СДР'");
	}
	printf("Отправлено сообщение 'Принять параметры СДР' c тестовыми данными\n");
	printf("Данные тела сообщения 'Принять параметры СДР' (первые 20 байт): "); // Выводим для проверки
	for (int i = 0; i < 20 && i < ntohs(prinyatParametrySdrMessage.header.body_length); ++i) {
		printf("%02X ", prinyatParametrySdrMessage.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
}

// Пункт 3.4.4.2
// [4.2.13] «Принять параметры 3ЦО» (отправка сообщения)
void send_prinyat_parametry_3tso(int clientSocketFD, uint16_t *messageCounter) {
	Message prinyatParametry3TsoMessage = create_prinyat_parametry_3tso_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, (*messageCounter)++);
	if (send_message(clientSocketFD, &prinyatParametry3TsoMessage) != 0) {
		perror("Ошибка отправки сообщения 'Принять параметры 3ЦО'");
	}
	printf("Отправлено сообщение 'Принять параметры 3ЦО' с тестовыми данными\n");
	printf("Данные тела сообщения 'Принять параметры 3ЦО' (первые 20 байт): "); // Выводим для проверки
	for (int i = 0; i < 20 && i < ntohs(prinyatParametry3TsoMessage.header.body_length); ++i) {
		printf("%02X ", prinyatParametry3TsoMessage.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
}

// [4.2.14] «Принять REF_AZIMUTH» (отправка сообщения)
void send_prinyat_ref_azimuth(int clientSocketFD, uint16_t *messageCounter) {
	Message msg = create_prinyat_ref_azimuth_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, (*messageCounter)++);
	 // Размер тела уже установлен (и, возможно, урезан) в create_prinyat_ref_azimuth_message
	if (send_message(clientSocketFD, &msg) != 0) {
		perror("Ошибка отправки сообщения 'Принять REF_AZIMUTH'");
	}
	printf("Отправлено сообщение 'Принять REF_AZIMUTH' (длина тела: %d байт)\n", ntohs(msg.header.body_length));
	printf("Данные тела сообщения 'Принять REF_AZIMUTH' (первые 20 байт): ");
	for (int i = 0; i < 20 && i < ntohs(msg.header.body_length); ++i) {
		printf("%02X ", msg.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
}

// Пункт 3.4.3.2.
// [4.2.15] «Принять параметры ЦДР» (отправка сообщения)
void send_prinyat_parametry_tsd(int clientSocketFD, uint16_t *messageCounter) {
	Message prinyatParametryTsdMessage = create_prinyat_parametry_tsd_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, (*messageCounter)++);
	if (send_message(clientSocketFD, &prinyatParametryTsdMessage) != 0) {
		perror("Ошибка отправки сообщения 'Принять параметры ЦДР'");
	}
	printf("Отправлено сообщение 'Принять параметры ЦДР' с тестовыми данными\n");
	printf("Данные тела сообщения 'Принять параметры ЦДР' (первые 20 байт): "); // Выводим для проверки
	for (int i = 0; i < 20 && i < ntohs(prinyatParametryTsdMessage.header.body_length); ++i) {
		printf("%02X ", prinyatParametryTsdMessage.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
}

// Пункт 3.4.3.2.
// [4.2.16] «Навигационные данные» (отправка сообщения)
void send_navigatsionnye_dannye(int clientSocketFD, uint16_t *messageCounter) {
	Message navigatsionnyeDannyeMessage = create_navigatsionnye_dannye_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, (*messageCounter)++);
	if (send_message(clientSocketFD, &navigatsionnyeDannyeMessage) != 0) {
		perror("Ошибка отправки сообщения 'Навигационные данные'");
	}
	printf("Отправлено сообщение 'Навигационные данные'\n");
	printf("Данные тела сообщения 'Навигационные данные' (первые 20 байт): "); // Выводим для проверки
	for (int i = 0; i < 20 && i < ntohs(navigatsionnyeDannyeMessage.header.body_length); ++i) {
		printf("%02X ", navigatsionnyeDannyeMessage.body[i]);
	}
	printf("...\n");
	sleep(DELAY_BETWEEN_MESSAGES_SEC);
}

int main(int argc, char* argv[] ) {
	/********************************************************************************/
	/*			УСТАНАВЛИВАЕМ СОЕДИНЕНИЕ С СВ-М И ВЫБИРАЕМ РЕЖИМ РАБОТЫ			*/
	/********************************************************************************/
	
	// --- Соединение по сети ---
	int clientSocketFD; 
	struct sockaddr_in serverAddress; 
	
	if ((clientSocketFD = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Не удалось создать сокет");
		exit(EXIT_FAILURE);
	}

	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(PORT_UVM);
	if (inet_pton(AF_INET, SVM_IP_ADDRESS, &serverAddress.sin_addr) <= 0) {
		perror("Ошибка преобразования адреса");
		exit(EXIT_FAILURE);
	}

	if (connect(clientSocketFD, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
		perror("Ошибка подключения");
		exit(EXIT_FAILURE);
	}

	printf("UVM подключен к SVM\n");
	
	// --- Выбор режима работы через аргументы командной строки ---
	RadarMode selectedMode = MODE_DR; // Режим по умолчанию - ДР

	if (argc > 1) {
		if (strcmp(argv[1], "OR") == 0) {
			selectedMode = MODE_OR;
		} else if (strcmp(argv[1], "OR1") == 0) {
			selectedMode = MODE_OR1;
		} else if (strcmp(argv[1], "DR") == 0) {
			selectedMode = MODE_DR;
		} else if (strcmp(argv[1], "VR") == 0) {
			selectedMode = MODE_VR;
		} else {
			fprintf(stderr, "Неверный режим работы. Используйте OR, OR1, DR или VR.\n");
			exit(EXIT_FAILURE);
		}
	}

	printf("Выбран режим работы: ");
	switch (selectedMode) {
		case MODE_OR:   printf("OR\n"); break;
		case MODE_OR1:  printf("OR1\n"); break;
		case MODE_DR:   printf("DR\n"); break;
		case MODE_VR:   printf("VR\n"); break;
	}
	
	/********************************************************************************/
	/*			  ВЗАИМОДЕЙСТВИЕ С СВ-М, ОБРАБОТКА ПРИНЯТЫХ СООБЩЕНИЙ			  */
	/********************************************************************************/
	
	uint16_t currentMessageCounter = 0;
	ConfirmInitBody *confirmInitBody;
	PodtverzhdenieKontrolyaBody *podtverzhdenieKontrolyaBody;
	RezultatyKontrolyaBody *rezultatyKontrolyaBody;
	SostoyanieLiniiBody *sostoyanieLiniiBody;
	Message receivedMessage; // Структура для приема ответа

	// --- ПОДГОТОВКА К СЕАНСУ НАБЛЮДЕНИЯ (РАЗДЕЛ 3.3) ---
	printf("\n--- Подготовка к сеансу наблюдения ---\n");

	// [4.2.1] «Инициализация канала» (отправка сообщения)
	// [4.2.2] «Подтверждение инициализации канала» (приём сообщения)
	// Если тип полученного сообщения корректен, то извлекается ConfirmInitBody, содержащее LAK, SLP, VRD, VOR1, VOR2, BCB
	confirmInitBody = send_init_channel_and_receive_confirm(clientSocketFD, &currentMessageCounter, &receivedMessage);
	if (confirmInitBody == NULL) {
		fprintf(stderr, "Не удалось инициализировать канал.\n");
		close(clientSocketFD);
		exit(EXIT_FAILURE);
	}
    // Доступ к данным через confirmInitBody (это указатель на тело внутри receivedMessage)
	printf("Получено сообщение подтверждения инициализации от SVM: LAK=0x%02X, SLP=0x%02X, VDR=0x%02X, VOR1=0x%02X, VOR2=0x%02X, BCB=0x%08X\n", // Форматирование SLP, VDR и т.д. как HEX
		   confirmInitBody->lak, confirmInitBody->slp, confirmInitBody->vdr, confirmInitBody->bop1, confirmInitBody->bop2, confirmInitBody->bcb);
	printf("Счетчик BCB из подтверждения инициализации: 0x%08X\n", confirmInitBody->bcb);
	sleep(DELAY_BETWEEN_MESSAGES_SEC);


	// [4.2.3] «Провести контроль» (отправка сообщения)
	// [4.2.4] «Подтверждение контроля» (приём сообщения)
	// Если тип полученного сообщения корректен, то извлекается PodtverzhdenieKontrolyaBody, содержащее LAK, TK, BCB
	podtverzhdenieKontrolyaBody = send_provesti_kontrol_and_receive_podtverzhdenie(clientSocketFD, &currentMessageCounter, &receivedMessage);
    if (podtverzhdenieKontrolyaBody == NULL) {
        fprintf(stderr, "Не удалось провести контроль.\n");
        close(clientSocketFD);
        exit(EXIT_FAILURE);
    }
	printf("Получено сообщение подтверждения контроля от SVM: LAK=0x%02X, TK=0x%02X, BCB=0x%08X\n",
		   podtverzhdenieKontrolyaBody->lak, podtverzhdenieKontrolyaBody->tk, podtverzhdenieKontrolyaBody->bcb);
	printf("Счетчик BCB из подтверждения контроля: 0x%08X\n", podtverzhdenieKontrolyaBody->bcb);
	sleep(DELAY_BETWEEN_MESSAGES_SEC);


	// [4.2.5] «Выдать результаты контроля» (отправка сообщения)
	// [4.2.6] «Результаты контроля» (отправка сообщения)
	// Если тип полученного сообщения корректен, то извлекается RezultatyKontrolyaBody, содержащее LAK, RSK, BCK, BCB
	rezultatyKontrolyaBody = send_vydat_rezultaty_kontrolya_and_receive_rezultaty(clientSocketFD, &currentMessageCounter, &receivedMessage);
    if (rezultatyKontrolyaBody == NULL) {
        fprintf(stderr, "Не удалось получить результаты контроля.\n");
        close(clientSocketFD);
        exit(EXIT_FAILURE);
    }
	printf("Получены результаты контроля от SVM: LAK=0x%02X, RSK=0x%02X, BCK=0x%04X, BCB=0x%08X\n", // Форматирование RSK как HEX
		   rezultatyKontrolyaBody->lak, rezultatyKontrolyaBody->rsk, rezultatyKontrolyaBody->vsk, rezultatyKontrolyaBody->bcb);
	printf("Счетчик BCB из результатов контроля: 0x%08X\n", rezultatyKontrolyaBody->bcb);
	sleep(DELAY_BETWEEN_MESSAGES_SEC);


	// [4.2.7] «Выдать состояние линии» (отправка сообщения)
	// [4.2.8] «Состояние линии» (отправка сообщения)
	// Если тип полученного сообщения корректен, то извлекается RezultatyKontrolyaBody, содержащее LAK, KLA, SLA, KSA, BCB
	// Для отладки выводиться первые 10 байт тела сообщения в сыром виде
	sostoyanieLiniiBody = send_vydat_sostoyanie_linii_and_receive_sostoyanie(clientSocketFD, &currentMessageCounter, &receivedMessage);
    if (sostoyanieLiniiBody == NULL) {
        fprintf(stderr, "Не удалось получить состояние линии.\n");
        close(clientSocketFD);
        exit(EXIT_FAILURE);
    }
	printf("Получено сообщение состояния линии от SVM: LAK=0x%02X, KLA=0x%04X, SLA=0x%08X, KSA=0x%04X, BCB=0x%08X\n",
		   sostoyanieLiniiBody->lak, sostoyanieLiniiBody->kla, sostoyanieLiniiBody->sla, sostoyanieLiniiBody->ksa, sostoyanieLiniiBody->bcb);
	printf("Счетчик BCB из состояния линии: 0x%08X\n", sostoyanieLiniiBody->bcb);
	printf("Сырые данные тела (первые 10 байт) состояния линии: ");
    // Доступ к телу через receivedMessage, так как sostoyanieLiniiBody указывает на него
	for (int i = 0; i < 10 && i < receivedMessage.header.body_length; ++i) { // Используем body_length из заголовка (уже в host order)
		printf("%02X ", receivedMessage.body[i]);
	}
	printf("\n");
	
	// ________________________________________________________________________________________________________________________________
	// --- ПОДГОТОВКА К СЕАНСУ СЪЁМКИ (РАЗДЕЛ 3.4) ---
	printf("\n--- Подготовка к сеансу съемки - ");

	if (selectedMode == MODE_DR) {
		printf("Режим ДР ---\n");
		send_prinyat_parametry_sdr(clientSocketFD, &currentMessageCounter);		// 4.2.12. «Принять параметры СДР»
		send_prinyat_parametry_tsd(clientSocketFD, &currentMessageCounter);		// 4.2.15. «Принять параметры ЦДР»
		send_navigatsionnye_dannye(clientSocketFD, &currentMessageCounter);		// 4.2.16. «Навигационные данные»
	} else if (selectedMode == MODE_VR) {
		printf("Режим ВР ---\n");
		send_prinyat_parametry_so(clientSocketFD, &currentMessageCounter);		// 4.2.9. «Принять параметры СО»
		send_prinyat_parametry_3tso(clientSocketFD, &currentMessageCounter);	// 4.2.13. «Принять параметры 3ЦО»
		send_navigatsionnye_dannye(clientSocketFD, &currentMessageCounter);		// 4.2.16. «Навигационные данные» 
	} else if (selectedMode == MODE_OR || selectedMode == MODE_OR1) {
		if (selectedMode == MODE_OR) printf("Режим ОР ---\n"); else printf("Режим ОР1 ---\n");

		send_prinyat_parametry_so(clientSocketFD, &currentMessageCounter);		// 4.2.9. «Принять параметры СО»
		send_prinyat_time_ref_range(clientSocketFD, &currentMessageCounter);	// 4.2.10. «Принять TIME_REF_RANGE»
		send_prinyat_reper(clientSocketFD, &currentMessageCounter);			 // 4.2.11. «Принять Reper»
		send_prinyat_parametry_3tso(clientSocketFD, &currentMessageCounter);	// 4.2.13. «Принять параметры 3ЦО»
		send_prinyat_ref_azimuth(clientSocketFD, &currentMessageCounter);		// 4.2.14. «Принять REF_AZIMUTH»
		send_navigatsionnye_dannye(clientSocketFD, &currentMessageCounter);		// 4.2.16. «Навигационные данные»
	}

	close(clientSocketFD);
	return 0;
}