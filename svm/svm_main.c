/*
 * svm/svm_main.c
 *
 * Описание:
 * Основной файл SVM: инициализация, загрузка конфигурации, создание интерфейса,
 * прослушивание, принятие соединения, главный цикл приема/обработки сообщений.
 */

#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../io/io_common.h"
#include "../io/io_interface.h"      // <-- Для IOInterface
#include "../config/config.h"        // <-- Для AppConfig
#include "svm_handlers.h"
#include "svm_timers.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // для strcasecmp
#include <unistd.h>
#include <arpa/inet.h> // Для inet_ntop (остальное не нужно)
#include <netinet/in.h> // Для sockaddr_in (остальное не нужно)
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>


int main() {
	int serverSocketFD = -1; // Слушающий дескриптор
    int clientSocketFD = -1; // Дескриптор клиента
    AppConfig config;
    IOInterface *io_svm = NULL;

	// --- Инициализация ---
	printf("SVM запуск...\n");

    // Загрузка конфигурации
    printf("SVM: Загрузка конфигурации...\n");
    if (load_config("config.ini", &config) != 0) {
        exit(EXIT_FAILURE);
    }

    // --- Создание интерфейса IO ---
    printf("SVM: Создание интерфейса типа '%s'...\n", config.interface_type);
     if (strcasecmp(config.interface_type, "ethernet") == 0) {
        io_svm = create_ethernet_interface(&config.ethernet);
    } else if (strcasecmp(config.interface_type, "serial") == 0) {
        // Для serial не нужен listen/accept в том же виде,
        // но для унификации можно вызвать connect/listen, который откроет порт
        io_svm = create_serial_interface(&config.serial);
        if(io_svm) {
            printf("SVM: Используется Serial интерфейс.\n");
            // Для serial "слушающий" дескриптор - это и есть дескриптор клиента
            clientSocketFD = io_svm->listen(io_svm); // Открываем и настраиваем порт
            if (clientSocketFD < 0) {
                fprintf(stderr, "SVM: Не удалось открыть/настроить COM порт %s. Завершение.\n", config.serial.device);
                io_svm->destroy(io_svm);
                exit(EXIT_FAILURE);
            }
             printf("SVM: COM порт %s открыт (handle: %d)\n", config.serial.device, clientSocketFD);
             // Для serial нет отдельного слушающего сокета
             serverSocketFD = -1; // Явно указываем, что слушающего сокета нет

        }
    } else {
         fprintf(stderr, "SVM: Неизвестный тип интерфейса '%s' в config.ini. Завершение.\n", config.interface_type);
         exit(EXIT_FAILURE);
    }

    if (!io_svm && strcasecmp(config.interface_type, "ethernet") == 0) { // Если Ethernet и не создан
         fprintf(stderr, "SVM: Не удалось создать Ethernet IOInterface. Завершение.\n");
         exit(EXIT_FAILURE);
    }
    if (!io_svm && strcasecmp(config.interface_type, "serial") == 0) { // Если Serial и не создан
         fprintf(stderr, "SVM: Не удалось создать Serial IOInterface. Завершение.\n");
         exit(EXIT_FAILURE);
    }


    // Инициализация диспетчера сообщений
	init_message_handlers();

	// --- Ожидание клиента (только для Ethernet) ---
    if (io_svm->type == IO_TYPE_ETHERNET) {
        printf("SVM: Запуск прослушивания Ethernet...\n");
        serverSocketFD = io_svm->listen(io_svm);
	    if (serverSocketFD < 0) {
		    fprintf(stderr, "SVM: Ошибка запуска прослушивания Ethernet. Завершение.\n");
            io_svm->destroy(io_svm);
		    exit(EXIT_FAILURE);
	    }
	    printf("SVM слушает на порту %d (handle: %d)\n", config.ethernet.port, serverSocketFD);

        char client_ip_str[INET_ADDRSTRLEN];
        uint16_t client_port_num;
        printf("SVM: Ожидание подключения UVM...\n");
        clientSocketFD = io_svm->accept(io_svm, client_ip_str, sizeof(client_ip_str), &client_port_num);

        // Закрываем слушающий сокет после принятия соединения
        if (serverSocketFD >= 0) {
             io_svm->disconnect(io_svm, serverSocketFD);
             // io_svm->io_handle все еще может хранить serverSocketFD, это нормально,
             // т.к. основной дескриптор для операций теперь clientSocketFD
        }


        if (clientSocketFD < 0) {
		    fprintf(stderr, "SVM: Ошибка принятия соединения. Завершение.\n");
            io_svm->destroy(io_svm);
		    exit(EXIT_FAILURE);
	    }
        printf("SVM принял соединение от UVM (%s:%u) (клиентский дескриптор: %d)\n",
           client_ip_str, client_port_num, clientSocketFD);
    }
    // Для Serial порта clientSocketFD уже установлен после io_svm->listen()

    if (clientSocketFD < 0) { // Дополнительная проверка
         fprintf(stderr, "SVM: Не удалось установить коммуникационный канал. Завершение.\n");
         io_svm->destroy(io_svm);
         exit(EXIT_FAILURE);
    }

	// --- Запуск таймеров ---
	if (start_timer(TIMER_INTERVAL_BCB_MS, bcbTimerHandler, SIGALRM) == -1) {
		fprintf(stderr, "Не удалось запустить таймер.\n");
        io_svm->disconnect(io_svm, clientSocketFD);
        io_svm->destroy(io_svm);
		exit(EXIT_FAILURE);
	}
	printf("Таймер запущен.\n");

	time_t lastPrintTime = time(NULL);

	// --- Главный цикл обработки сообщений ---
	while (1) {
		Message receivedMessage;

        // Используем receive_protocol_message с дескриптором клиента/порта
		int recvStatus = receive_protocol_message(io_svm, clientSocketFD, &receivedMessage);

		if (recvStatus == -1) {
			fprintf(stderr, "SVM: Ошибка получения сообщения от клиента %d. Завершение.\n", clientSocketFD);
			break;
		} else if (recvStatus == 1) {
			printf("SVM: Соединение с UVM (клиент %d) закрыто.\n", clientSocketFD);
			break;
        } else if (recvStatus == -2 && io_svm->type == IO_TYPE_SERIAL) {
            // Для serial порта receive_data может вернуть -2 (таймаут poll или EINTR)
            // Просто продолжаем цикл, ожидая данных
            continue;
        }


		// Сообщение успешно получено
		MessageHandler handler = message_handlers[receivedMessage.header.message_type];
		if (handler != NULL) {
            handler(io_svm, clientSocketFD, &receivedMessage); // Передаем io_svm
		} else {
			printf("SVM: Неизвестный тип сообщения от клиента %d: %u (номер %u)\n",
                   clientSocketFD,
                   receivedMessage.header.message_type,
                   get_full_message_number(&receivedMessage.header));
		}

        // Периодический вывод счетчиков
		time_t currentTime = time(NULL);
        // Используем константу из svm_timers.h
		if (currentTime - lastPrintTime >= COUNTER_PRINT_INTERVAL_SEC) {
            if (receivedMessage.header.message_type != MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII) {
			    print_counters();
            }
			lastPrintTime = currentTime;
		}
	}

	// --- Завершение ---
	printf("SVM завершает работу.\n");
    // Закрываем соединение/порт через интерфейс
    if (clientSocketFD >= 0) {
	    io_svm->disconnect(io_svm, clientSocketFD);
    }
    // Освобождаем ресурсы интерфейса
    io_svm->destroy(io_svm);

	return 0;
}