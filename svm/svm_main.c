/*
 * svm/svm_main.c
 *
 * Описание:
 * Основной файл SVM: инициализация сети, главный цикл приема сообщений,
 * вызов обработчиков из диспетчера.
 */

#include "../protocol/protocol_defs.h" // Определения протокола
#include "../protocol/message_utils.h" // Утилиты
#include "../io/io_common.h"          // Функции send/receive
#include "svm_handlers.h"            // Обработчики и диспетчер
#include "svm_timers.h"              // Таймеры и счетчики

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>

// Константы (можно вынести в config)
#define PORT_SVM 8080
#define COUNTER_PRINT_INTERVAL_SEC 5

int main() {
	int serverSocketFD, clientSocketFD;
	struct sockaddr_in serverAddress, clientAddress;
	socklen_t clientAddressLength;

	// --- Инициализация ---
	printf("SVM запуск...\n");
	init_message_handlers(); // Инициализация диспетчера сообщений

	// --- Сетевая часть ---
	if ((serverSocketFD = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("Не удалось создать сокет");
		exit(EXIT_FAILURE);
	}
    // Установка опции для переиспользования адреса
    int opt = 1;
    if (setsockopt(serverSocketFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt(SO_REUSEADDR) failed");
        // Не фатально, можно продолжить, но могут быть проблемы с быстрым перезапуском
    }


	memset(&serverAddress, 0, sizeof(serverAddress));
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = INADDR_ANY;
	serverAddress.sin_port = htons(PORT_SVM);

	if (bind(serverSocketFD, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
		perror("Ошибка привязки");
        close(serverSocketFD);
		exit(EXIT_FAILURE);
	}

	if (listen(serverSocketFD, 1) < 0) { // Достаточно 1 ожидающего соединения
		perror("Ошибка прослушивания");
        close(serverSocketFD);
		exit(EXIT_FAILURE);
	}

	printf("SVM слушает на порту %d\n", PORT_SVM);

	clientAddressLength = sizeof(clientAddress);
	if ((clientSocketFD = accept(serverSocketFD, (struct sockaddr *)&clientAddress, &clientAddressLength)) < 0) {
		perror("Ошибка принятия соединения");
        close(serverSocketFD);
		exit(EXIT_FAILURE);
	}
	close(serverSocketFD); // Закрываем слушающий сокет, он больше не нужен

	printf("SVM принял соединение от UVM (клиентский дескриптор: %d)\n", clientSocketFD);


	// --- Запуск таймеров ---
	if (start_timer(TIMER_INTERVAL_BCB_MS, bcbTimerHandler, SIGALRM) == -1) {
		fprintf(stderr, "Не удалось запустить таймер.\n");
        close(clientSocketFD);
		exit(EXIT_FAILURE);
	}
	printf("Таймер запущен.\n");

	time_t lastPrintTime = time(NULL); // Для периодического вывода счетчиков

	// --- Главный цикл обработки сообщений ---
	while (1) {
		Message receivedMessage; // Структура для хранения полученного сообщения

		int recvStatus = receive_full_message(clientSocketFD, &receivedMessage);

		if (recvStatus == -1) {
			fprintf(stderr, "SVM: Ошибка получения сообщения. Завершение.\n");
			break; // Выход из цикла при ошибке
		} else if (recvStatus == 1) {
			printf("SVM: Соединение с UVM закрыто.\n");
			break; // Выход из цикла при закрытии соединения
		}

		// Сообщение успешно получено и преобразовано в хост-порядок
		MessageHandler handler = message_handlers[receivedMessage.header.message_type];
		if (handler != NULL) {
			// Передаем дескриптор клиента и полученное сообщение
            handler(clientSocketFD, &receivedMessage);
		} else {
			printf("SVM: Неизвестный тип сообщения: %u (номер %u)\n",
                   receivedMessage.header.message_type,
                   get_full_message_number(&receivedMessage.header));
		}

        // Периодический вывод счетчиков
		time_t currentTime = time(NULL);
		if (currentTime - lastPrintTime >= COUNTER_PRINT_INTERVAL_SEC) {
            // Не выводим счетчики, если только что обработали запрос на их вывод
            if (receivedMessage.header.message_type != MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII) {
			    print_counters();
            }
			lastPrintTime = currentTime;
		}
	}

	// --- Завершение ---
	printf("SVM завершает работу.\n");
	close(clientSocketFD); // Закрываем сокет клиента

	return 0;
}