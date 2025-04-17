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
#include <sys/time.h> // Для setitimer
#include "common.h"

// Константы
#define PORT_SVM 8080
#define TIMER_INTERVAL_BCB_MS 50
#define TIMER_INTERVAL_LINK_STATUS_MS 2000
#define COUNTER_PRINT_INTERVAL_SEC 5

// Глобальные переменные
volatile uint32_t bcbCounter = 0;
uint16_t linkUpChangesCounter = 0;
uint32_t linkUpLowTimeSeconds = 0;
uint16_t signDetChangesCounter = 0;
SVMState currentSvmState = STATE_NOT_INITIALIZED;
uint16_t currentMessageCounter = 0;

// --- Прототипы функций-обработчиков сообщений ---
void handle_init_channel_message(int clientSocketFD, Message *receivedMessage);
void handle_confirm_init_message(int clientSocketFD, Message *receivedMessage); // Заглушка
void handle_provesti_kontrol_message(int clientSocketFD, Message *receivedMessage);
void handle_podtverzhdenie_kontrolya_message(int clientSocketFD, Message *receivedMessage); // Заглушка
void handle_vydat_rezultaty_kontrolya_message(int clientSocketFD, Message *receivedMessage);
void handle_rezultaty_kontrolya_message(int clientSocketFD, Message *receivedMessage); // Заглушка
void handle_vydat_sostoyanie_linii_message(int clientSocketFD, Message *receivedMessage);
void handle_sostoyanie_linii_message(int clientSocketFD, Message *receivedMessage); // Заглушка
void handle_sostoyanie_linii_136_message(int clientSocketFD, Message *receivedMessage); // Заглушка
void handle_vydat_sostoyanie_linii_137_message(int clientSocketFD, Message *receivedMessage);
void handle_sostoyanie_linii_138_message(int clientSocketFD, Message *receivedMessage); // Заглушка
// --- НОВОЕ: Прототип обработчика сообщения "Принять параметры СДР" ---
void handle_prinyat_parametry_sdr_message(int clientSocketFD, Message *receivedMessage);


// --- Объявление типа указателя на функцию обработчика сообщений ---
typedef void (*MessageHandler)(int clientSocketFD, Message *message);

// --- Объявление массива указателей на функции обработчиков ---
MessageHandler message_handlers[256];

// Функция: Обработчик таймера BCB
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

// Функция: Запуск таймера
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

// Функция: Вывод счетчиков SVM
void print_counters() {
    printf("\n--- Счетчики SVM ---\n");
    printf("Счетчик BCB: 0x%08X\n", bcbCounter);
    printf("Изменения LinkUp: %u\n", linkUpChangesCounter);
    printf("Время низкого уровня LinkUp: %u с\n", linkUpLowTimeSeconds);
    printf("Изменения SignDet: %u\n", signDetChangesCounter);
    printf("--- Конец счетчиков ---\n");
}

// --- Реализация функций-обработчиков сообщений ---

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

void handle_confirm_init_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение Confirm Init (не ожидается)\n"); // Заглушка, SVM не должен получать этот тип сообщения
}

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

void handle_podtverzhdenie_kontrolya_message(int clientSocketFD, Message *receivedMessage) {
     printf("SVM получил сообщение Podtverzhdenie Kontrolya (не ожидается)\n"); // Заглушка, SVM не должен получать этот тип сообщения
}

void handle_vydat_rezultaty_kontrolya_message(int clientSocketFD, Message *receivedMessage) {
    printf("Получено сообщение выдать результаты контроля\n"); // Пункт 3.3.6

    uint8_t rsk = 0x01;
    uint16_t bck = 100;
    Message resultsMessage = create_rezultaty_kontrolya_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, rsk, bck, bcbCounter, currentMessageCounter++);
    if (send_message(clientSocketFD, &resultsMessage) != 0) {
        exit(EXIT_FAILURE);
    }
    printf("Отправлено сообщение с результатами контроля\n"); // Пункт 3.3.6
}

void handle_rezultaty_kontrolya_message(int clientSocketFD, Message *receivedMessage) {
     printf("SVM получил сообщение Rezultaty Kontrolya (не ожидается)\n"); // Заглушка, SVM не должен получать этот тип сообщения
}


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

void handle_sostoyanie_linii_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение Sostoyanie Linii (не ожидается)\n"); // Заглушка, SVM не должен получать этот тип сообщения
}

void handle_sostoyanie_linii_136_message(int clientSocketFD, Message *receivedMessage) {
     printf("SVM получил сообщение Sostoyanie Linii 136 (не ожидается)\n"); // Заглушка, SVM не должен получать этот тип сообщения
}

void handle_vydat_sostoyanie_linii_137_message(int clientSocketFD, Message *receivedMessage) {
    printf("Получено сообщение выдать состояние линии 137 (заглушка)\n"); // Заглушка, используется для тестирования 4.2.7
    Message sostoyanieMessage = create_sostoyanie_linii_138_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1, bcbCounter, currentMessageCounter++);
    if (send_message(clientSocketFD, &sostoyanieMessage) != 0) {
        exit(EXIT_FAILURE);
    }
    printf("Отправлено сообщение состояния линии 138 (заглушка)\n"); // Заглушка, используется для тестирования 4.2.8
}

void handle_sostoyanie_linii_138_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение Sostoyanie Linii 138 (не ожидается)\n"); // Заглушка, SVM не должен получать этот тип сообщения
}

// --- НОВОЕ: Заглушка для обработчика сообщения "Принять параметры СДР" ---
void handle_prinyat_parametry_sdr_message(int clientSocketFD, Message *receivedMessage) {
    printf("SVM получил сообщение 'Принять параметры СДР' (заглушка)\n");
    // Здесь будет реальная обработка сообщения "Принять параметры СДР" в будущем
}


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
    message_handlers[MESSAGE_TYPE_SOSTOYANIE_LINII_136]      = handle_sostoyanie_linii_136_message; // Заглушка, SVM не должен получать этот тип сообщения
    message_handlers[MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII_137] = handle_vydat_sostoyanie_linii_137_message;
    message_handlers[MESSAGE_TYPE_SOSTOYANIE_LINII_138]      = handle_sostoyanie_linii_138_message; // Заглушка, SVM не должен получать этот тип сообщения
    // --- НОВОЕ: Регистрация обработчика для сообщения "Принять параметры СДР" ---
    message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR]     = handle_prinyat_parametry_sdr_message;
}


int main() {
    int serverSocketFD, clientSocketFD;
    struct sockaddr_in serverAddress, clientAddress;
    socklen_t clientAddressLength;
    Message receivedMessage;
    srand(time(NULL));

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

    if (start_timer(TIMER_INTERVAL_BCB_MS, bcbTimerHandler, SIGALRM) == -1) {
        fprintf(stderr, "Не удалось запустить таймер.\n");
        exit(EXIT_FAILURE);
    }
    printf("Таймер запущен.\n");

    init_message_handlers(); // Инициализация диспетчера

    time_t lastPrintTime = time(NULL);
    while (1) {
        memset(&receivedMessage, 0, sizeof(Message));

        ssize_t bytesReceived;
        do {
            bytesReceived = recv(clientSocketFD, &receivedMessage, sizeof(Message), 0);
        } while (bytesReceived < 0 && errno == EINTR);

        if (bytesReceived < 0) {
            perror("Ошибка получения данных");
            exit(EXIT_FAILURE);
        } else if (bytesReceived == 0) {
            printf("Соединение закрыто UVM.\n");
            close(clientSocketFD);
            close(serverSocketFD);
            exit(EXIT_SUCCESS);
        } else if ((size_t)bytesReceived < sizeof(MessageHeader)) {
            printf("Получен неполный заголовок сообщения.\n");
            close(clientSocketFD);
            close(serverSocketFD);
            exit(EXIT_FAILURE);
        }

        message_to_host_byte_order(&receivedMessage);

        // --- Диспетчеризация сообщения ---
        MessageHandler handler = message_handlers[receivedMessage.header.message_type];
        if (handler != NULL) {
            handler(clientSocketFD, &receivedMessage);
        } else {
            printf("SVM: Неизвестный тип сообщения: %u\n", receivedMessage.header.message_type);
        }

        // --- Фаза 2: Вывод счетчиков (при необходимости) ---
        time_t currentTime = time(NULL);
        if (currentTime - lastPrintTime >= COUNTER_PRINT_INTERVAL_SEC && receivedMessage.header.message_type != MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII) {
            print_counters();
            lastPrintTime = currentTime;
        }
        sleep(1);
    }

    close(clientSocketFD);
    close(serverSocketFD);
    return 0;
}