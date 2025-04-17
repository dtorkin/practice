/*
 * svm/svm_main.c
 *
 * Описание:
 * Основной файл SVM: инициализация сети, главный цикл приема сообщений,
 * вызов обработчиков из диспетчера.
 */

#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../io/io_common.h"
#include "../io/io_interface.h"      // <-- Включаем для IOInterface
#include "../config/config.h"        // <-- Включаем для AppConfig, load_config
#include "svm_handlers.h"
#include "svm_timers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // <-- Добавлено для strcasecmp
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>

int main() {
	int serverSocketFD = -1, clientSocketFD = -1; // Инициализируем -1
	struct sockaddr_in clientAddress;
	socklen_t clientAddressLength;
    AppConfig config;
    IOInterface *io_svm = NULL; // Указатель на интерфейс IO

	// --- Инициализация ---
	printf("SVM запуск...\n");

    // Загрузка конфигурации
    printf("SVM: Загрузка конфигурации...\n");
    if (load_config("config.ini", &config) != 0) {
        fprintf(stderr, "SVM: Не удалось загрузить или обработать config.ini. Завершение.\n");
        exit(EXIT_FAILURE);
    }

    // --- Создание и настройка интерфейса IO ---
    if (strcasecmp(config.interface_type, "ethernet") == 0) {
        printf("SVM: Используется Ethernet интерфейс.\n");
        // Создаем копию Ethernet конфигурации для передачи фабрике
        EthernetConfig eth_conf;
        eth_conf.base.type = IO_TYPE_ETHERNET; // Устанавливаем тип явно
        strncpy(eth_conf.target_ip, "0.0.0.0", sizeof(eth_conf.target_ip)); // SVM слушает на всех IP
        eth_conf.target_ip[sizeof(eth_conf.target_ip)-1] = '\0';
        eth_conf.port = config.ethernet.port;

        io_svm = create_ethernet_interface(&eth_conf);
    } else if (strcasecmp(config.interface_type, "serial") == 0) {
        printf("SVM: Используется Serial интерфейс.\n");
        // io_svm = create_serial_interface(&config.serial); // <- Будет добавлено позже
        fprintf(stderr, "SVM: Serial интерфейс пока не реализован. Завершение.\n");
         exit(EXIT_FAILURE); // Пока не реализовано
    } else {
         fprintf(stderr, "SVM: Неизвестный тип интерфейса '%s' в config.ini. Завершение.\n", config.interface_type);
         exit(EXIT_FAILURE);
    }

    if (!io_svm) {
         fprintf(stderr, "SVM: Не удалось создать IOInterface. Завершение.\n");
         exit(EXIT_FAILURE);
    }

    // Инициализация диспетчера сообщений ПОСЛЕ создания интерфейса (если он нужен)
	init_message_handlers();

	// --- Сетевая/Портовая часть (через интерфейс) ---
    printf("SVM: Запуск прослушивания...\n");
    serverSocketFD = io_svm->listen(io_svm); // Используем функцию интерфейса
	if (serverSocketFD < 0) {
		fprintf(stderr, "SVM: Ошибка запуска прослушивания. Завершение.\n");
        io_svm->destroy(io_svm); // Освобождаем интерфейс
		exit(EXIT_FAILURE);
	}
    // io_svm->io_handle теперь содержит слушающий дескриптор

	printf("SVM слушает на порту %d (handle: %d)\n", config.ethernet.port, serverSocketFD);

    // --- Ожидание клиента ---
	clientAddressLength = sizeof(clientAddress);
    char client_ip_str[INET_ADDRSTRLEN];
    uint16_t client_port_num;

    // Используем функцию accept из интерфейса
    clientSocketFD = io_svm->accept(io_svm, client_ip_str, sizeof(client_ip_str), &client_port_num);

    // Слушающий сокет больше не нужен после принятия соединения (для одного клиента)
    // io_svm->disconnect(io_svm, serverSocketFD); // Закрываем слушающий handle
    // io_svm->io_handle = -1; // Сбрасываем основной handle интерфейса, т.к. он больше не слушающий

    if (clientSocketFD < 0) {
		fprintf(stderr, "SVM: Ошибка принятия соединения. Завершение.\n");
        io_svm->destroy(io_svm); // Освобождаем интерфейс
		exit(EXIT_FAILURE);
	}

    printf("SVM принял соединение от UVM (%s:%u) (клиентский дескриптор: %d)\n",
           client_ip_str, client_port_num, clientSocketFD);


	// --- Запуск таймеров ---
	if (start_timer(TIMER_INTERVAL_BCB_MS, bcbTimerHandler, SIGALRM) == -1) {
		fprintf(stderr, "Не удалось запустить таймер.\n");
        io_svm->disconnect(io_svm, clientSocketFD); // Закрываем клиента
        io_svm->destroy(io_svm); // Освобождаем интерфейс
		exit(EXIT_FAILURE);
	}
	printf("Таймер запущен.\n");

	time_t lastPrintTime = time(NULL);

	// --- Главный цикл обработки сообщений ---
	while (1) {
		Message receivedMessage;

        // Используем новую функцию receive_protocol_message и передаем io_svm и дескриптор клиента
		int recvStatus = receive_protocol_message(io_svm, clientSocketFD, &receivedMessage);

		if (recvStatus == -1) {
			fprintf(stderr, "SVM: Ошибка получения сообщения от клиента %d. Завершение.\n", clientSocketFD);
			break; // Выход из цикла при ошибке
		} else if (recvStatus == 1) {
			printf("SVM: Соединение с UVM (клиент %d) закрыто.\n", clientSocketFD);
			break; // Выход из цикла при закрытии соединения
		}

		// Сообщение успешно получено
		MessageHandler handler = message_handlers[receivedMessage.header.message_type];
		if (handler != NULL) {
			// Передаем io_svm и дескриптор клиента в обработчик
            handler(io_svm, clientSocketFD, &receivedMessage);
		} else {
			printf("SVM: Неизвестный тип сообщения от клиента %d: %u (номер %u)\n",
                   clientSocketFD,
                   receivedMessage.header.message_type,
                   get_full_message_number(&receivedMessage.header));
		}

        // Периодический вывод счетчиков
		time_t currentTime = time(NULL);
		if (currentTime - lastPrintTime >= COUNTER_PRINT_INTERVAL_SEC) {
            if (receivedMessage.header.message_type != MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII) {
			    print_counters();
            }
			lastPrintTime = currentTime;
		}
	}

	// --- Завершение ---
	printf("SVM завершает работу.\n");
    if (clientSocketFD >= 0) {
	    io_svm->disconnect(io_svm, clientSocketFD); // Закрываем сокет клиента через интерфейс
    }
    io_svm->destroy(io_svm); // Освобождаем ресурсы интерфейса

	return 0;
}