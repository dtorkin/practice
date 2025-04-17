/*
 * uvm/uvm_main.c
 *
 * Описание:
 * Основной файл UVM: инициализация сети, разбор аргументов (режим),
 * вызов функций взаимодействия из uvm_comm.
 */

#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../io/io_common.h"
#include "../io/io_interface.h"      // <-- Включаем для IOInterface
#include "../config/config.h"        // <-- Включаем для AppConfig
#include "uvm_comm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // <-- Добавлено для strcasecmp
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>


int main(int argc, char* argv[] ) {
	int clientSocketFD = -1; // Дескриптор соединения
	uint16_t currentMessageCounter = 0;
	Message receivedMessage; // Буфер для приема ответов
    AppConfig config;
    IOInterface *io_uvm = NULL; // Указатель на интерфейс IO

	// --- Загрузка конфигурации ---
    printf("UVM: Загрузка конфигурации...\n");
    if (load_config("config.ini", &config) != 0) {
        fprintf(stderr, "UVM: Не удалось загрузить или обработать config.ini. Завершение.\n");
        exit(EXIT_FAILURE);
    }

    // --- Создание интерфейса IO ---
     if (strcasecmp(config.interface_type, "ethernet") == 0) {
        printf("UVM: Используется Ethernet интерфейс.\n");
        io_uvm = create_ethernet_interface(&config.ethernet); // Используем фабрику
    } else if (strcasecmp(config.interface_type, "serial") == 0) {
        printf("UVM: Используется Serial интерфейс.\n");
        // io_uvm = create_serial_interface(&config.serial); // Будет добавлено
        fprintf(stderr, "UVM: Serial интерфейс пока не реализован. Завершение.\n");
         exit(EXIT_FAILURE);
    } else {
         fprintf(stderr, "UVM: Неизвестный тип интерфейса '%s' в config.ini. Завершение.\n", config.interface_type);
         exit(EXIT_FAILURE);
    }

    if (!io_uvm) {
         fprintf(stderr, "UVM: Не удалось создать IOInterface. Завершение.\n");
         exit(EXIT_FAILURE);
    }

	// --- Выбор режима работы ---
	RadarMode selectedMode = MODE_DR;
	if (argc > 1) {
        if (strcasecmp(argv[1], "OR") == 0) selectedMode = MODE_OR; // Используем strcasecmp
		else if (strcasecmp(argv[1], "OR1") == 0) selectedMode = MODE_OR1;
		else if (strcasecmp(argv[1], "DR") == 0) selectedMode = MODE_DR;
		else if (strcasecmp(argv[1], "VR") == 0) selectedMode = MODE_VR;
		else {
			fprintf(stderr, "Неверный режим работы. Используйте OR, OR1, DR или VR.\n");
            io_uvm->destroy(io_uvm);
			exit(EXIT_FAILURE);
		}
	}

	// --- Сетевое подключение (через интерфейс) ---
    printf("UVM: Подключение к SVM...\n");
    clientSocketFD = io_uvm->connect(io_uvm); // Используем функцию интерфейса
	if (clientSocketFD < 0) {
		fprintf(stderr, "UVM: Ошибка подключения к SVM. Завершение.\n");
        io_uvm->destroy(io_uvm);
		exit(EXIT_FAILURE);
	}
	// io_uvm->io_handle теперь содержит дескриптор клиента

	printf("UVM подключен к SVM (%s:%d) (handle: %d)\n",
           ((EthernetConfig*)io_uvm->config)->target_ip, // Доступ к конфигу через указатель
           ((EthernetConfig*)io_uvm->config)->port,
           clientSocketFD);

	printf("Выбран режим работы: ");
	switch (selectedMode) {
		case MODE_OR:   printf("OR\n"); break;
		case MODE_OR1:  printf("OR1\n"); break;
		case MODE_DR:   printf("DR\n"); break;
		case MODE_VR:   printf("VR\n"); break;
        // default: // Не должно случиться из-за проверки выше
        //    break;
	}

	// --- ВЗАИМОДЕЙСТВИЕ С SVM (через uvm_comm, передавая io_uvm) ---

	// --- ПОДГОТОВКА К СЕАНСУ НАБЛЮДЕНИЯ ---
	printf("\n--- Подготовка к сеансу наблюдения ---\n");

    // Передаем io_uvm вместо clientSocketFD
	ConfirmInitBody* confirmInitBody = send_init_channel_and_receive_confirm(io_uvm, &currentMessageCounter, &receivedMessage);
	if (confirmInitBody == NULL) { goto cleanup_and_exit; }
	printf("Получено сообщение подтверждения инициализации от SVM: LAK=0x%02X, SLP=0x%02X, VDR=0x%02X, BOP1=0x%02X, BOP2=0x%02X, BCB=0x%08X\n",
		   confirmInitBody->lak, confirmInitBody->slp, confirmInitBody->vdr, confirmInitBody->bop1, confirmInitBody->bop2, confirmInitBody->bcb);
	printf("Счетчик BCB из подтверждения инициализации: 0x%08X\n", confirmInitBody->bcb);

    uint8_t tk_request = 0x01;
    PodtverzhdenieKontrolyaBody* podtverzhdenieKontrolyaBody = send_provesti_kontrol_and_receive_podtverzhdenie(io_uvm, &currentMessageCounter, &receivedMessage, tk_request);
	if (podtverzhdenieKontrolyaBody == NULL) { goto cleanup_and_exit; }
    printf("Получено сообщение подтверждения контроля от SVM: LAK=0x%02X, TK=0x%02X, BCB=0x%08X\n",
		   podtverzhdenieKontrolyaBody->lak, podtverzhdenieKontrolyaBody->tk, podtverzhdenieKontrolyaBody->bcb);
	printf("Счетчик BCB из подтверждения контроля: 0x%08X\n", podtverzhdenieKontrolyaBody->bcb);

    uint8_t vpk_request = 0x01;
    RezultatyKontrolyaBody* rezultatyKontrolyaBody = send_vydat_rezultaty_kontrolya_and_receive_rezultaty(io_uvm, &currentMessageCounter, &receivedMessage, vpk_request);
	if (rezultatyKontrolyaBody == NULL) { goto cleanup_and_exit; }
    printf("Получены результаты контроля от SVM: LAK=0x%02X, RSK=0x%02X, VSK=0x%04X, BCB=0x%08X\n",
		   rezultatyKontrolyaBody->lak, rezultatyKontrolyaBody->rsk, rezultatyKontrolyaBody->vsk, rezultatyKontrolyaBody->bcb);
	printf("Счетчик BCB из результатов контроля: 0x%08X\n", rezultatyKontrolyaBody->bcb);

    SostoyanieLiniiBody* sostoyanieLiniiBody = send_vydat_sostoyanie_linii_and_receive_sostoyanie(io_uvm, &currentMessageCounter, &receivedMessage);
	if (sostoyanieLiniiBody == NULL) { goto cleanup_and_exit; }
	printf("Получено сообщение состояния линии от SVM: LAK=0x%02X, KLA=0x%04X, SLA=0x%08X, KSA=0x%04X, BCB=0x%08X\n",
		   sostoyanieLiniiBody->lak, sostoyanieLiniiBody->kla, sostoyanieLiniiBody->sla, sostoyanieLiniiBody->ksa, sostoyanieLiniiBody->bcb);
	printf("Счетчик BCB из состояния линии: 0x%08X\n", sostoyanieLiniiBody->bcb);
	printf("Сырые данные тела (первые 10 байт) состояния линии: ");
	for (int i = 0; i < 10 && i < receivedMessage.header.body_length; ++i) {
		printf("%02X ", receivedMessage.body[i]);
	}
	printf("\n");


	// --- ПОДГОТОВКА К СЕАНСУ СЪЁМКИ ---
	printf("\n--- Подготовка к сеансу съемки - ");
    int send_status = 0;

	if (selectedMode == MODE_DR) {
		printf("Режим ДР ---\n");
		send_status |= send_prinyat_parametry_sdr(io_uvm, &currentMessageCounter);
		send_status |= send_prinyat_parametry_tsd(io_uvm, &currentMessageCounter);
		send_status |= send_navigatsionnye_dannye(io_uvm, &currentMessageCounter);
	} else if (selectedMode == MODE_VR) {
		printf("Режим ВР ---\n");
		send_status |= send_prinyat_parametry_so(io_uvm, &currentMessageCounter);
		send_status |= send_prinyat_parametry_3tso(io_uvm, &currentMessageCounter);
		send_status |= send_navigatsionnye_dannye(io_uvm, &currentMessageCounter);
	} else if (selectedMode == MODE_OR || selectedMode == MODE_OR1) {
		if (selectedMode == MODE_OR) printf("Режим ОР ---\n"); else printf("Режим ОР1 ---\n");
		send_status |= send_prinyat_parametry_so(io_uvm, &currentMessageCounter);
		send_status |= send_prinyat_time_ref_range(io_uvm, &currentMessageCounter);
		send_status |= send_prinyat_reper(io_uvm, &currentMessageCounter);
		send_status |= send_prinyat_parametry_3tso(io_uvm, &currentMessageCounter);
		send_status |= send_prinyat_ref_azimuth(io_uvm, &currentMessageCounter);
		send_status |= send_navigatsionnye_dannye(io_uvm, &currentMessageCounter);
	}

    if(send_status != 0) {
        fprintf(stderr, "UVM: Произошла ошибка при отправке сообщений подготовки к съемке.\n");
        goto cleanup_and_exit; // Используем goto для очистки
    }

	printf("\nUVM: Завершение работы после отправки параметров.\n");

cleanup_and_exit: // Метка для очистки перед выходом
    if (io_uvm != NULL) {
        if (clientSocketFD >= 0) { // Если соединение было установлено
            io_uvm->disconnect(io_uvm, clientSocketFD);
            printf("UVM: Соединение закрыто.\n");
        }
        io_uvm->destroy(io_uvm); // Освобождаем ресурсы интерфейса
        printf("UVM: Интерфейс IO освобожден.\n");
    } else if (clientSocketFD >= 0) {
        // Если интерфейс не создан, но сокет был (маловероятно)
        close(clientSocketFD);
    }

	return (send_status != 0 || confirmInitBody == NULL || podtverzhdenieKontrolyaBody == NULL || rezultatyKontrolyaBody == NULL || sostoyanieLiniiBody == NULL) ? EXIT_FAILURE : EXIT_SUCCESS;
}