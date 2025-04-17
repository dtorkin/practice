// svm.c

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
#include "common.h"

// Константы
#define PORT_SVM 8080
#define TIMER_INTERVAL_BCB_MS 50
#define TIMER_INTERVAL_LINK_STATUS_MS 2000
#define COUNTER_PRINT_INTERVAL_SEC 5

SVMState currentSvmState = STATE_NOT_INITIALIZED;                       // Текущее состояние СВ-М

// --- Прототипы функций-обработчиков сообщений ---
void handle_init_channel_message(int clientSocketFD, Message *receivedMessage);                 // 4.2.1. «Инициализация канала»
void handle_confirm_init_message(int clientSocketFD, Message *receivedMessage);                 // 4.2.2. «Подтверждение инициализации канала» (заглушка)
void handle_provesti_kontrol_message(int clientSocketFD, Message *receivedMessage);             // 4.2.3. «Провести контроль»
void handle_podtverzhdenie_kontrolya_message(int clientSocketFD, Message *receivedMessage);     // 4.2.4. «Подтверждение контроля» (заглушка)
void handle_vydat_rezultaty_kontrolya_message(int clientSocketFD, Message *receivedMessage);    // 4.2.5. «Выдать результаты контроля»
void handle_rezultaty_kontrolya_message(int clientSocketFD, Message *receivedMessage);          // 4.2.6. «Результаты контроля» (заглушка)
void handle_vydat_sostoyanie_linii_message(int clientSocketFD, Message *receivedMessage);       // 4.2.7. «Выдать состояние линии»
void handle_sostoyanie_linii_message(int clientSocketFD, Message *receivedMessage);             // 4.2.8. «Состояние линии» (заглушка)
void handle_prinyat_parametry_so_message(int clientSocketFD, Message *receivedMessage);         // 4.2.9. «Принять параметры СО» (заглушка)
void handle_prinyat_parametry_sdr_message(int clientSocketFD, Message *receivedMessage);        // 4.2.12. «Принять параметры СДР» (заглушка)
void handle_prinyat_parametry_3tso_message(int clientSocketFD, Message *receivedMessage);		// 4.2.13. «Принять параметры 3ЦО» 
void handle_prinyat_parametry_tsd_message(int clientSocketFD, Message *receivedMessage);        // 4.2.15. «Принять параметры ЦДР» (заглушка)
void handle_navigatsionnye_dannye_message(int clientSocketFD, Message *receivedMessage);        // 4.2.16. «Навигационные данные» (заглушка)

/********************************************************************************/
/*                               СЧЁТЧИКИ [3.3.3]                               */
/********************************************************************************/

uint16_t currentMessageCounter = 0;
volatile uint32_t bcbCounter = 0;   // Счётчик времени работы СВ-М (32 бита), увеличивается каждые 50 мс.
uint16_t linkUpChangesCounter = 0;  // Счётчик изменений состояния триггера LinkUp с «1» на «0» (16 бит)
uint32_t linkUpLowTimeSeconds = 0;  // Время, в течение которого триггер LinkUp находился в состоянии «0» (32 бита, в секундах).
uint16_t signDetChangesCounter = 0; // Счётчик изменений состояния триггера SignDet с «1» на «0» (16 бит).

// Обработчик таймера BCB
void bcbTimerHandler(int sig) {
    (void)sig;
    static int linkStatusTimerCounter = 0;

    bcbCounter++;

    linkStatusTimerCounter++;
    if (linkStatusTimerCounter >= (TIMER_INTERVAL_LINK_STATUS_MS / TIMER_INTERVAL_BCB_MS)) {
        linkStatusTimerCounter = 0;
        if (rand() % 2 == 0) {
            linkUpChangesCounter++;
            if (rand() % 10 == 0) {
                linkUpLowTimeSeconds += (TIMER_INTERVAL_LINK_STATUS_MS / 1000);
            }
        }
        if (rand() % 3 == 0) {
            signDetChangesCounter++;
        }
    }
}

// Запуск таймера
int start_timer(int interval_ms, void (*handler)(int), int sig) {
    struct sigaction sa;
    struct itimerval timer;

    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(sig, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    timer.it_value.tv_sec = interval_ms / 1000;
    timer.it_value.tv_usec = (interval_ms % 1000) * 1000;
    timer.it_interval.tv_sec = interval_ms / 1000;
    timer.it_interval.tv_usec = (interval_ms % 1000) * 1000;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {
        perror("setitimer");
        return -1;
    }
    return 0;
}

// Вывод счетчиков SVM
void print_counters() {
    printf("\n--- Счетчики SVM ---\n");
    printf("Счетчик BCB: 0x%08X\n", bcbCounter);
    printf("Изменения LinkUp: %u\n", linkUpChangesCounter);
    printf("Время низкого уровня LinkUp: %u с\n", linkUpLowTimeSeconds);
    printf("Изменения SignDet: %u\n", signDetChangesCounter);
    printf("--- Конец счетчиков ---\n");
}

/********************************************************************************/
/*                        ОБРАБОТЧИКИ СООБЩЕНИЙ (handle_)                        */
/********************************************************************************/

// [4.2.1] «Инициализация канала» (обработчик сообщений)
void handle_init_channel_message(int clientSocketFD, Message *receivedMessage) {
    printf("Получено сообщение инициализации канала\n"); // Пункт 3.2
    uint16_t receivedMessageNumber = get_full_message_number(&receivedMessage->header);
    printf("Номер полученного сообщения: %u\n", receivedMessageNumber);
    InitChannelBody *body = (InitChannelBody *)receivedMessage->body;
    printf("Получено сообщение инициализации канала от UVM: LAUVM=0x%02X, LAK=0x%02X\n", body->lauvm, body->lak); // Пункт 4.2.1
    printf("SVM: Эмуляция выключения лазера в неиспользуемом канале...\n"); // Пункт 3.3.3

    uint8_t slp = 0x03;
    uint8_t vdr = 0x01;
    uint8_t vor1 = 0x02;
    uint8_t vor2 = 0x03;

    Message confirmMessage = create_confirm_init_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, slp, vdr, vor1, vor2, bcbCounter, currentMessageCounter++);
    if (send_message(clientSocketFD, &confirmMessage) != 0) {
        exit(EXIT_FAILURE);
    }
    printf("Отправлено сообщение подтверждения инициализации\n"); // Пункт 3.2

    currentSvmState = STATE_INITIALIZED;
}

// [4.2.2] «Подтверждение инициализации канала» (обработчик сообщений) - заглушка
void handle_confirm_init_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение Confirm Init (не ожидается)\n"); // Заглушка, SVM не должен получать этот тип сообщения
}

// [4.2.3] «Провести контроль» (обработчик сообщений)
void handle_provesti_kontrol_message(int clientSocketFD, Message *receivedMessage) {
    printf("Получено сообщение провести контроль\n"); // Пункт 3.3.5
    currentSvmState = STATE_SELF_TEST;
    printf("SVM: Эмуляция самопроверки...\n"); // Пункт 3.3.5
    sleep(3);
    currentSvmState = STATE_INITIALIZED;
    ProvestiKontrolBody *receivedBody = (ProvestiKontrolBody *)receivedMessage->body;
    Message confirmMessage = create_podtverzhdenie_kontrolya_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, receivedBody->tk, bcbCounter, currentMessageCounter++);
    if (send_message(clientSocketFD, &confirmMessage) != 0) {
        exit(EXIT_FAILURE);
    }
    printf("Отправлено сообщение подтверждения контроля\n"); // Пункт 3.3.5
}

// [4.2.4] «Подтверждение контроля» (обработчик сообщений) - заглушка
void handle_podtverzhdenie_kontrolya_message(int clientSocketFD, Message *receivedMessage) {
     printf("SVM получил сообщение Podtverzhdenie Kontrolya (не ожидается)\n"); // Заглушка, SVM не должен получать этот тип сообщения
}

// [4.2.5] «Выдать результаты контроля» (обработчик сообщений)
void handle_vydat_rezultaty_kontrolya_message(int clientSocketFD, Message *receivedMessage) {
    printf("Получено сообщение выдать результаты контроля\n");

    uint8_t rsk = 0x01; // фиксированные значения
    uint16_t bck = 100; // фиксированные значения
    Message resultsMessage = create_rezultaty_kontrolya_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, rsk, bck, bcbCounter, currentMessageCounter++);
    if (send_message(clientSocketFD, &resultsMessage) != 0) {
        exit(EXIT_FAILURE);
    }
    printf("Отправлено сообщение с результатами контроля\n");
}

// [4.2.6] «Результаты контроля» (обработчик сообщений) - заглушка
void handle_rezultaty_kontrolya_message(int clientSocketFD, Message *receivedMessage) {
     printf("SVM получил сообщение Rezultaty Kontrolya (не ожидается)\n"); // Заглушка, SVM не должен получать этот тип сообщения
}

// [4.2.7] «Выдать состояние линии» (обработчик сообщений)
void handle_vydat_sostoyanie_linii_message(int clientSocketFD, Message *receivedMessage) {
    printf("Получено сообщение выдать состояние линии\n"); // Пункт 3.3.8

    print_counters(); // Выводим счетчики перед отправкой для синхронизации

    Message sostoyanieMessage = create_sostoyanie_linii_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1,
                                                               linkUpChangesCounter, linkUpLowTimeSeconds, signDetChangesCounter, bcbCounter, currentMessageCounter++);
    if (send_message(clientSocketFD, &sostoyanieMessage) != 0) {
        exit(EXIT_FAILURE);
    }
    printf("Отправлено сообщение состояния линии\n"); // Пункт 3.3.8
}

// [4.2.8] «Состояние линии» (обработчик сообщений) - заглушка
void handle_sostoyanie_linii_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение Sostoyanie Linii (не ожидается)\n"); // Заглушка, SVM не должен получать этот тип сообщения
}

// [4.2.9] «Принять параметры СО» (обработчик сообщений) - заглушка
void handle_prinyat_parametry_so_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение 'Принять параметры СО' (заглушка)\n");
    // Здесь будет реальная обработка сообщения "Принять параметры СО" в будущем
}

// [4.2.12] «Принять параметры СДР» (обработчик сообщений) - заглушка
void handle_prinyat_parametry_sdr_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение 'Принять параметры СДР' (заглушка)\n");
    // Здесь будет реальная обработка сообщения "Принять параметры СДР" в будущем
}

// [4.2.13] «Принять параметры 3ЦО» (обработчик сообщений) - заглушка
void handle_prinyat_parametry_3tso_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение 'Принять параметры 3ЦО' (заглушка)\n");
    // Здесь будет реальная обработка сообщения "Принять параметры 3ЦО"
    // Нужно будет извлечь данные из receivedMessage->body, кастуя его к (PrinyatParametry3TsoBody*)
    // и использовать эти параметры для настройки СВ-М.
    PrinyatParametry3TsoBody *body = (PrinyatParametry3TsoBody *)receivedMessage->body;
    printf("  Ncadr: %u\n", body->Ncadr); // Пример вывода одного поля
    printf("  Xnum: %u\n", body->Xnum);
    // ... (вывод других полей для отладки при необходимости) ...
}

// [4.2.15] «Принять параметры ЦДР» (обработчик сообщений) - заглушка
void handle_prinyat_parametry_tsd_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение 'Принять параметры ЦДР' (заглушка)\n");
    // Здесь будет реальная обработка сообщения "Принять параметры ЦДР" в будущем
}

// [4.2.16] «Навигационные данные» (обработчик сообщений) - заглушка
void handle_navigatsionnye_dannye_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение 'Навигационные данные' (заглушка)\n");
    // Здесь будет реальная обработка сообщения "Навигационные данные" в будущем
}

/********************************************************************************/
/*                   ГОТОВИМ УКАЗАТЕЛИ НА ФУНКЦИИ-ОБРАБОТЧИКИ                   */
/********************************************************************************/

typedef void (*MessageHandler)(int clientSocketFD, Message *message);   // Тип указателей на функции обработчиков
MessageHandler message_handlers[256];                                   // Массив указателей на функции обработчкиков

// --- Функция инициализации диспетчера сообщений ---
void init_message_handlers() {
    for (int i = 0; i < 256; ++i) {
        message_handlers[i] = NULL; // Инициализируем все указатели NULL
    }
    message_handlers[MESSAGE_TYPE_INIT_CHANNEL]             = handle_init_channel_message;
    message_handlers[MESSAGE_TYPE_CONFIRM_INIT]             = handle_confirm_init_message; // Заглушка, SVM не должен получать этот тип сообщения
    message_handlers[MESSAGE_TYPE_PROVESTI_KONTROL]         = handle_provesti_kontrol_message;
    message_handlers[MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA] = handle_podtverzhdenie_kontrolya_message; // Заглушка, SVM не должен получать этот тип сообщения
    message_handlers[MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA] = handle_vydat_rezultaty_kontrolya_message;
    message_handlers[MESSAGE_TYPE_RESULTATY_KONTROLYA]      = handle_rezultaty_kontrolya_message; // Заглушка, SVM не должен получать этот тип сообщения
    message_handlers[MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII]    = handle_vydat_sostoyanie_linii_message;
    message_handlers[MESSAGE_TYPE_SOSTOYANIE_LINII]          = handle_sostoyanie_linii_message; // Заглушка, SVM не должен получать этот тип сообщения
    message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR]     = handle_prinyat_parametry_sdr_message;
    message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD]    = handle_prinyat_parametry_tsd_message;
    message_handlers[MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE]    = handle_navigatsionnye_dannye_message;
	message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_SO]      = handle_prinyat_parametry_so_message;
	message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO]    = handle_prinyat_parametry_3tso_message; 
}


int main() {
	/********************************************************************************/
	/*              УСТАНАВЛИВАЕМ СОЕДИНЕНИЕ С УВМ И ЗАПУСКАЕМ ТАЙМЕРЫ              */
	/********************************************************************************/

	// --- Соединение по сети ---
    int serverSocketFD, clientSocketFD;
    struct sockaddr_in serverAddress, clientAddress;
    socklen_t clientAddressLength;

    if ((serverSocketFD = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Не удалось создать сокет");
        exit(EXIT_FAILURE);
    }

    memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons(PORT_SVM);

    if (bind(serverSocketFD, (struct sockaddr *)&serverAddress, sizeof(serverAddress)) < 0) {
        perror("Ошибка привязки");
        exit(EXIT_FAILURE);
    }

    if (listen(serverSocketFD, 5) < 0) {
        perror("Ошибка прослушивания");
        exit(EXIT_FAILURE);
    }

    printf("SVM слушает на порту %d\n", PORT_SVM);

    clientAddressLength = sizeof(clientAddress);
    if ((clientSocketFD = accept(serverSocketFD, (struct sockaddr *)&clientAddress, &clientAddressLength)) < 0) {
        perror("Ошибка принятия соединения");
        exit(EXIT_FAILURE);
    }

    printf("SVM принял соединение от UVM\n");

	// --- Запускаем таймеры ---
    srand(time(NULL));

    if (start_timer(TIMER_INTERVAL_BCB_MS, bcbTimerHandler, SIGALRM) == -1) {
        fprintf(stderr, "Не удалось запустить таймер.\n");
        exit(EXIT_FAILURE);
    }
    printf("Таймер запущен.\n");

	time_t lastPrintTime = time(NULL);

	/********************************************************************************/
	/*              ВЗАИМОДЕЙСТВИЕ С УВМ, ОБРАБОТКА ПРИНЯТЫХ СООБЩЕНИЙ              */
	/********************************************************************************/
	init_message_handlers(); // Инициализация диспетчера (тот что описан выше)
	// Message receivedMessage; // Убрали отсюда, создаем в цикле

    while (1) {
        Message receivedMessage;
        MessageHeader header; // Буфер только для заголовка
        ssize_t bytesRead;
        size_t totalBytesRead;

        // --- Этап 1: Чтение заголовка ---
        totalBytesRead = 0;
        while (totalBytesRead < sizeof(MessageHeader)) {
            do {
                bytesRead = recv(clientSocketFD, ((char*)&header) + totalBytesRead, sizeof(MessageHeader) - totalBytesRead, 0);
            } while (bytesRead < 0 && errno == EINTR);

            if (bytesRead < 0) {
                perror("Ошибка получения заголовка");
                exit(EXIT_FAILURE);
            } else if (bytesRead == 0) {
                printf("Соединение закрыто UVM.\n");
                close(clientSocketFD);
                close(serverSocketFD);
                exit(EXIT_SUCCESS);
            }
            totalBytesRead += bytesRead;
        }

        // Копируем прочитанный заголовок в основную структуру
        memcpy(&receivedMessage.header, &header, sizeof(MessageHeader));

        // --- Этап 2: Преобразование длины тела и чтение тела ---
        // Сначала преобразуем длину тела в host order
        uint16_t bodyLen = ntohs(receivedMessage.header.body_length);

        if (bodyLen > MAX_MESSAGE_BODY_SIZE) {
             printf("Ошибка: Длина тела (%u) превышает максимальный размер (%d).\n", bodyLen, MAX_MESSAGE_BODY_SIZE);
             // Можно добавить обработку ошибки, например, пропуск сообщения или разрыв соединения
             continue; // Пропустить это сообщение
        }

        // Читаем тело сообщения нужной длины
        totalBytesRead = 0;
        while (totalBytesRead < bodyLen) {
            do {
                bytesRead = recv(clientSocketFD, receivedMessage.body + totalBytesRead, bodyLen - totalBytesRead, 0);
            } while (bytesRead < 0 && errno == EINTR);

            if (bytesRead < 0) {
                perror("Ошибка получения тела сообщения");
                // Можно решить разорвать соединение или пропустить сообщение
                exit(EXIT_FAILURE); // Пример: завершение при ошибке чтения тела
            } else if (bytesRead == 0) {
                printf("Соединение закрыто UVM во время чтения тела.\n");
                close(clientSocketFD);
                close(serverSocketFD);
                exit(EXIT_SUCCESS);
            }
            totalBytesRead += bytesRead;
        }

        // --- Этап 3: Полная обработка сообщения ---
        // Теперь у нас есть полный заголовок и тело. Выполняем преобразование для остальных полей.
        // Восстанавливаем body_length в host order для остальной логики (т.к. message_to_host_byte_order ожидает network order)
        receivedMessage.header.body_length = htons(bodyLen); // Временно возвращаем в network order для функции
        message_to_host_byte_order(&receivedMessage); // Теперь преобразуем остальные поля

        // Вызываем обработчик
        MessageHandler handler = message_handlers[receivedMessage.header.message_type];
        if (handler != NULL) {
            handler(clientSocketFD, &receivedMessage);
        } else {
            printf("SVM: Неизвестный тип сообщения: %u\n", receivedMessage.header.message_type);
        }

		// Переодический вызов счётчиков (зависит от COUNTER_PRINT_INTERVAL_SEC)
        time_t currentTime = time(NULL);
        // Проверяем, что тип не Vydat Sostoyanie Linii, чтобы избежать двойного вывода
        if (currentTime - lastPrintTime >= COUNTER_PRINT_INTERVAL_SEC &&
            receivedMessage.header.message_type != MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII) {
            print_counters();
            lastPrintTime = currentTime;
        }
        // Убрал sleep(1) отсюда, так как recv теперь блокирующий и ждет данных
    }

    close(clientSocketFD);
    close(serverSocketFD);
    return 0;
}