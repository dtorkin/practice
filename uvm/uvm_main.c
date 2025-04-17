/*
 * uvm/uvm_main.c
 *
 * Описание:
 * Основной файл UVM: инициализация сети, разбор аргументов (режим),
 * вызов функций взаимодействия из uvm_comm.
 */

#include "../protocol/protocol_defs.h" // Определения протокола
#include "../protocol/message_utils.h" // Утилиты (если нужны здесь)
#include "../io/io_common.h"          // Функции send/receive (хотя они внутри uvm_comm)
#include "uvm_comm.h"                 // Функции взаимодействия

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

// Константы (можно вынести в config)
#define SVM_IP_ADDRESS "127.0.0.1" // Используем localhost для тестов
#define PORT_UVM 8080 // Порт SVM, к которому подключаемся

int main(int argc, char* argv[] ) {
	int clientSocketFD;
	struct sockaddr_in serverAddress;
	uint16_t currentMessageCounter = 0;
	Message receivedMessage; // Буфер для приема ответов

	// --- Выбор режима работы ---
	RadarMode selectedMode = MODE_DR; // Режим по умолчанию - ДР
	if (argc > 1) {
		if (strcmp(argv[1], "OR") == 0) selectedMode = MODE_OR;
		else if (strcmp(argv[1], "OR1") == 0) selectedMode = MODE_OR1;
		else if (strcmp(argv[1], "DR") == 0) selectedMode = MODE_DR;
		else if (strcmp(argv[1], "VR") == 0) selectedMode = MODE_VR;
		else {
			fprintf(stderr, "Неверный режим работы. Используйте OR, OR1, DR или VR.\n");
			exit(EXIT_FAILURE);
		}
	}

	// --- Сетевая часть ---
	if ((clientSocketFD = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Не удалось создать сокет");
		exit(EXIT_FAILURE);
	}

	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_port = htons(PORT_UVM);
	if (inet_pton(AF_INET, SVM_IP_ADDRESS, &serverAddress.sin_addr) <= 0) {
		perror("Ошибка преобразования адреса");
        close(clientSocketFD);
		exit(EXIT_FAILURE);
	}

	if (connect(clientSocketFD, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
		perror("Ошибка подключения к SVM");
        close(clientSocketFD);
		exit(EXIT_FAILURE);
	}
	printf("UVM подключен к SVM\n");

	printf("Выбран режим работы: ");
	switch (selectedMode) {
		case MODE_OR:   printf("OR\n"); break;
		case MODE_OR1:  printf("OR1\n"); break;
		case MODE_DR:   printf("DR\n"); break;
		case MODE_VR:   printf("VR\n"); break;
	}

	// --- ВЗАИМОДЕЙСТВИЕ С SVM ---

	// --- ПОДГОТОВКА К СЕАНСУ НАБЛЮДЕНИЯ (РАЗДЕЛ 3.3) ---
	printf("\n--- Подготовка к сеансу наблюдения ---\n");

    // Инициализация канала
	ConfirmInitBody* confirmInitBody = send_init_channel_and_receive_confirm(clientSocketFD, &currentMessageCounter, &receivedMessage);
	if (confirmInitBody == NULL) { goto cleanup_and_exit; } // Используем goto для очистки
	printf("Получено сообщение подтверждения инициализации от SVM: LAK=0x%02X, SLP=0x%02X, VDR=0x%02X, BOP1=0x%02X, BOP2=0x%02X, BCB=0x%08X\n",
		   confirmInitBody->lak, confirmInitBody->slp, confirmInitBody->vdr, confirmInitBody->bop1, confirmInitBody->bop2, confirmInitBody->bcb);
	printf("Счетчик BCB из подтверждения инициализации: 0x%08X\n", confirmInitBody->bcb);


    // Провести контроль
    uint8_t tk_request = 0x01; // Пример: запрашиваем самоконтроль СВ-М
    PodtverzhdenieKontrolyaBody* podtverzhdenieKontrolyaBody = send_provesti_kontrol_and_receive_podtverzhdenie(clientSocketFD, &currentMessageCounter, &receivedMessage, tk_request);
	if (podtverzhdenieKontrolyaBody == NULL) { goto cleanup_and_exit; }
    printf("Получено сообщение подтверждения контроля от SVM: LAK=0x%02X, TK=0x%02X, BCB=0x%08X\n",
		   podtverzhdenieKontrolyaBody->lak, podtverzhdenieKontrolyaBody->tk, podtverzhdenieKontrolyaBody->bcb);
	printf("Счетчик BCB из подтверждения контроля: 0x%08X\n", podtverzhdenieKontrolyaBody->bcb);


    // Выдать результаты контроля
    uint8_t vpk_request = 0x01; // Пример: Запросить результаты самоконтроля
    RezultatyKontrolyaBody* rezultatyKontrolyaBody = send_vydat_rezultaty_kontrolya_and_receive_rezultaty(clientSocketFD, &currentMessageCounter, &receivedMessage, vpk_request);
	if (rezultatyKontrolyaBody == NULL) { goto cleanup_and_exit; }
    printf("Получены результаты контроля от SVM: LAK=0x%02X, RSK=0x%02X, VSK=0x%04X, BCB=0x%08X\n", // Используем VSK
		   rezultatyKontrolyaBody->lak, rezultatyKontrolyaBody->rsk, rezultatyKontrolyaBody->vsk, rezultatyKontrolyaBody->bcb);
	printf("Счетчик BCB из результатов контроля: 0x%08X\n", rezultatyKontrolyaBody->bcb);


    // Выдать состояние линии
    SostoyanieLiniiBody* sostoyanieLiniiBody = send_vydat_sostoyanie_linii_and_receive_sostoyanie(clientSocketFD, &currentMessageCounter, &receivedMessage);
	if (sostoyanieLiniiBody == NULL) { goto cleanup_and_exit; }
	printf("Получено сообщение состояния линии от SVM: LAK=0x%02X, KLA=0x%04X, SLA=0x%08X, KSA=0x%04X, BCB=0x%08X\n",
		   sostoyanieLiniiBody->lak, sostoyanieLiniiBody->kla, sostoyanieLiniiBody->sla, sostoyanieLiniiBody->ksa, sostoyanieLiniiBody->bcb);
	printf("Счетчик BCB из состояния линии: 0x%08X\n", sostoyanieLiniiBody->bcb);
	printf("Сырые данные тела (первые 10 байт) состояния линии: ");
	for (int i = 0; i < 10 && i < receivedMessage.header.body_length; ++i) {
		printf("%02X ", receivedMessage.body[i]);
	}
	printf("\n");


	// --- ПОДГОТОВКА К СЕАНСУ СЪЁМКИ (РАЗДЕЛ 3.4) ---
	printf("\n--- Подготовка к сеансу съемки - ");
    int send_status = 0;

	if (selectedMode == MODE_DR) {
		printf("Режим ДР ---\n");
		send_status |= send_prinyat_parametry_sdr(clientSocketFD, &currentMessageCounter);
		send_status |= send_prinyat_parametry_tsd(clientSocketFD, &currentMessageCounter);
		send_status |= send_navigatsionnye_dannye(clientSocketFD, &currentMessageCounter);
	} else if (selectedMode == MODE_VR) {
		printf("Режим ВР ---\n");
		send_status |= send_prinyat_parametry_so(clientSocketFD, &currentMessageCounter);
		send_status |= send_prinyat_parametry_3tso(clientSocketFD, &currentMessageCounter);
		send_status |= send_navigatsionnye_dannye(clientSocketFD, &currentMessageCounter);
	} else if (selectedMode == MODE_OR || selectedMode == MODE_OR1) {
		if (selectedMode == MODE_OR) printf("Режим ОР ---\n"); else printf("Режим ОР1 ---\n");
		send_status |= send_prinyat_parametry_so(clientSocketFD, &currentMessageCounter);
		send_status |= send_prinyat_time_ref_range(clientSocketFD, &currentMessageCounter);
		send_status |= send_prinyat_reper(clientSocketFD, &currentMessageCounter);
		send_status |= send_prinyat_parametry_3tso(clientSocketFD, &currentMessageCounter);
		send_status |= send_prinyat_ref_azimuth(clientSocketFD, &currentMessageCounter);
		send_status |= send_navigatsionnye_dannye(clientSocketFD, &currentMessageCounter);
	}

    if(send_status != 0) {
        fprintf(stderr, "UVM: Произошла ошибка при отправке сообщений подготовки к съемке.\n");
        // goto можно использовать, но здесь просто выйдем
        close(clientSocketFD);
        exit(EXIT_FAILURE);
    }

	// --- Завершение работы UVM (пример) ---
    printf("\nUVM: Завершение работы после отправки параметров.\n");

cleanup_and_exit:
	close(clientSocketFD);
	printf("UVM: Соединение закрыто.\n");
	return 0;
}