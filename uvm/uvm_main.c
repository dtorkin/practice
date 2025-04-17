/*
 * uvm/uvm_main.c
 *
 * Описание:
 * Основной файл UVM: инициализация, загрузка конфигурации,
 * создание и подключение через IOInterface, вызов функций взаимодействия.
 */

#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../io/io_common.h"
#include "../io/io_interface.h"      // <--- Для IOInterface, EthernetConfig, SerialConfig
#include "../config/config.h"        // <--- Для AppConfig, load_config
#include "uvm_comm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // для strcasecmp
#include <unistd.h>
// #include <arpa/inet.h>   // Больше не нужны здесь
// #include <netinet/in.h>
#include <errno.h>

#define DELAY_BETWEEN_MESSAGES_SEC 1

int main(int argc, char* argv[] ) {
	int comm_handle = -1; // Дескриптор соединения/порта
	uint16_t currentMessageCounter = 0;
	Message receivedMessage;
    AppConfig config;
    IOInterface *io_uvm = NULL; // Указатель на интерфейс IO

	// --- Загрузка конфигурации ---
    printf("UVM: Загрузка конфигурации...\n");
    if (load_config("config.ini", &config) != 0) {
        // load_config уже выводит сообщение об ошибке
        exit(EXIT_FAILURE);
    }

    // --- Создание интерфейса IO на основе конфигурации ---
    printf("UVM: Создание интерфейса типа '%s'...\n", config.interface_type);
    if (strcasecmp(config.interface_type, "ethernet") == 0) {
        io_uvm = create_ethernet_interface(&config.ethernet);
    } else if (strcasecmp(config.interface_type, "serial") == 0) {
        io_uvm = create_serial_interface(&config.serial);
    } else {
         fprintf(stderr, "UVM: Неподдерживаемый тип интерфейса '%s' в config.ini.\n", config.interface_type);
         exit(EXIT_FAILURE);
    }

    if (!io_uvm) {
         fprintf(stderr, "UVM: Не удалось создать IOInterface. Завершение.\n");
         exit(EXIT_FAILURE);
    }

	// --- Выбор режима работы ---
	RadarMode selectedMode = MODE_DR; // Режим по умолчанию
	if (argc > 1) {
        if (strcasecmp(argv[1], "OR") == 0) selectedMode = MODE_OR;
		else if (strcasecmp(argv[1], "OR1") == 0) selectedMode = MODE_OR1;
		else if (strcasecmp(argv[1], "DR") == 0) selectedMode = MODE_DR;
		else if (strcasecmp(argv[1], "VR") == 0) selectedMode = MODE_VR;
		else {
			fprintf(stderr, "Неверный режим работы. Используйте OR, OR1, DR или VR.\n");
            io_uvm->destroy(io_uvm); // Освобождаем интерфейс перед выходом
			exit(EXIT_FAILURE);
		}
	}

	// --- Подключение к SVM (через интерфейс) ---
    printf("UVM: Подключение к SVM через %s...\n", config.interface_type);
    comm_handle = io_uvm->connect(io_uvm); // Используем функцию интерфейса
	if (comm_handle < 0) {
		fprintf(stderr, "UVM: Ошибка подключения к SVM. Завершение.\n");
        io_uvm->destroy(io_uvm);
		exit(EXIT_FAILURE);
	}
    // io_uvm->io_handle теперь содержит активный дескриптор

	printf("UVM: Успешно подключено (handle: %d)\n", comm_handle);


	printf("Выбран режим работы: ");
	switch (selectedMode) {
		case MODE_OR:   printf("OR\n"); break;
		case MODE_OR1:  printf("OR1\n"); break;
		case MODE_DR:   printf("DR\n"); break;
		case MODE_VR:   printf("VR\n"); break;
	}

	// --- ВЗАИМОДЕЙСТВИЕ С SVM (через uvm_comm, передавая io_uvm) ---
    // Переменные для хранения указателей на тела ответов
	ConfirmInitBody* confirmInitBody = NULL;
	PodtverzhdenieKontrolyaBody* podtverzhdenieKontrolyaBody = NULL;
	RezultatyKontrolyaBody* rezultatyKontrolyaBody = NULL;
	SostoyanieLiniiBody* sostoyanieLiniiBody = NULL;

	// --- ПОДГОТОВКА К СЕАНСУ НАБЛЮДЕНИЯ ---
	printf("\n--- Подготовка к сеансу наблюдения ---\n");

    // Инициализация канала
    // Передаем io_uvm вместо clientSocketFD
	confirmInitBody = send_init_channel_and_receive_confirm(io_uvm, &currentMessageCounter, &receivedMessage);
	if (confirmInitBody == NULL) { goto cleanup_and_exit; }
	printf("Получено подтверждение инициализации от SVM: LAK=0x%02X, SLP=0x%02X, VDR=0x%02X, BOP1=0x%02X, BOP2=0x%02X, BCB=0x%08X\n",
		   confirmInitBody->lak, confirmInitBody->slp, confirmInitBody->vdr, confirmInitBody->bop1, confirmInitBody->bop2, confirmInitBody->bcb);
	printf("Счетчик BCB из подтверждения инициализации: 0x%08X\n", confirmInitBody->bcb);
    sleep(DELAY_BETWEEN_MESSAGES_SEC); // Задержка между шагами

    // Провести контроль
    uint8_t tk_request = 0x01; // Запрашиваем самоконтроль СВ-М + контроль ОЗУ ОР1/ОР/ДР
    podtverzhdenieKontrolyaBody = send_provesti_kontrol_and_receive_podtverzhdenie(io_uvm, &currentMessageCounter, &receivedMessage, tk_request);
	if (podtverzhdenieKontrolyaBody == NULL) { goto cleanup_and_exit; }
    printf("Получено подтверждение контроля от SVM: LAK=0x%02X, TK=0x%02X, BCB=0x%08X\n",
		   podtverzhdenieKontrolyaBody->lak, podtverzhdenieKontrolyaBody->tk, podtverzhdenieKontrolyaBody->bcb);
	printf("Счетчик BCB из подтверждения контроля: 0x%08X\n", podtverzhdenieKontrolyaBody->bcb);
    sleep(DELAY_BETWEEN_MESSAGES_SEC);

    // Выдать результаты контроля
    uint8_t vpk_request = 0x0F; // Пример: Запросить все результаты ОЗУ (биты 1,2,3) и сам результат контроля (бит 0)
    rezultatyKontrolyaBody = send_vydat_rezultaty_kontrolya_and_receive_rezultaty(io_uvm, &currentMessageCounter, &receivedMessage, vpk_request);
	if (rezultatyKontrolyaBody == NULL) { goto cleanup_and_exit; }
    printf("Получены результаты контроля от SVM: LAK=0x%02X, RSK=0x%02X, VSK=0x%04X, BCB=0x%08X\n",
		   rezultatyKontrolyaBody->lak, rezultatyKontrolyaBody->rsk, rezultatyKontrolyaBody->vsk, rezultatyKontrolyaBody->bcb);
	printf("Счетчик BCB из результатов контроля: 0x%08X\n", rezultatyKontrolyaBody->bcb);
    sleep(DELAY_BETWEEN_MESSAGES_SEC);

    // Выдать состояние линии
    sostoyanieLiniiBody = send_vydat_sostoyanie_linii_and_receive_sostoyanie(io_uvm, &currentMessageCounter, &receivedMessage);
	if (sostoyanieLiniiBody == NULL) { goto cleanup_and_exit; }
	printf("Получено сообщение состояния линии от SVM: LAK=0x%02X, KLA=0x%04X, SLA=0x%08X, KSA=0x%04X, BCB=0x%08X\n",
		   sostoyanieLiniiBody->lak, sostoyanieLiniiBody->kla, sostoyanieLiniiBody->sla, sostoyanieLiniiBody->ksa, sostoyanieLiniiBody->bcb);
	printf("Счетчик BCB из состояния линии: 0x%08X\n", sostoyanieLiniiBody->bcb);
	printf("Сырые данные тела (первые 10 байт) состояния линии: ");
	for (int i = 0; i < 10 && i < receivedMessage.header.body_length; ++i) {
		printf("%02X ", receivedMessage.body[i]);
	}
	printf("\n");
    sleep(DELAY_BETWEEN_MESSAGES_SEC);


	// --- ПОДГОТОВКА К СЕАНСУ СЪЁМКИ ---
	printf("\n--- Подготовка к сеансу съемки - ");
    int send_status = 0;

    // Передаем io_uvm во все функции отправки
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
        goto cleanup_and_exit;
    }

	printf("\nUVM: Завершение работы после отправки параметров.\n");

cleanup_and_exit: // Метка для очистки перед выходом
    if (io_uvm != NULL) {
        if (comm_handle >= 0) { // Если соединение было установлено
            io_uvm->disconnect(io_uvm, comm_handle); // Закрываем через интерфейс
            printf("UVM: Соединение закрыто (handle: %d).\n", comm_handle);
        }
        io_uvm->destroy(io_uvm); // Освобождаем ресурсы интерфейса
        printf("UVM: Интерфейс IO освобожден.\n");
    }

    // Определяем код возврата
    int exit_code = EXIT_SUCCESS;
    if (send_status != 0 || confirmInitBody == NULL || podtverzhdenieKontrolyaBody == NULL || rezultatyKontrolyaBody == NULL || sostoyanieLiniiBody == NULL) {
        exit_code = EXIT_FAILURE;
    }

	return exit_code;
}