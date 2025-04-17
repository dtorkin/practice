/*
 * svm/svm_main.c
 *
 * Описание:
 * Основной файл SVM: инициализация, создание потоков (приемник, обработчик,
 * отправитель, таймер), управление их жизненным циклом.
 */

// --- Сначала системные заголовки ---
#include <stdio.h>      // Для printf, fprintf, stderr, perror
#include <stdlib.h>     // Для exit, EXIT_FAILURE, malloc, free (хотя malloc/free в других модулях)
#include <string.h>     // Для memset, strcasecmp (в config.c)
#include <strings.h>    // Для strcasecmp (используется здесь)
#include <unistd.h>     // Для close, sleep, usleep (если нужно)
#include <arpa/inet.h>  // Для inet_ntop, htons (в config.c)
#include <netinet/in.h> // Для sockaddr_in, INADDR_ANY
#include <sys/socket.h> // Для socket, bind, listen, accept, shutdown, SHUT_RDWR
#include <time.h>       // Для time_t
#include <signal.h>     // Для signal, sigaction, SIGINT, SIGTERM
#include <errno.h>      // Для errno
#include <pthread.h>    // Для потоков
#include <stdbool.h>    // Для bool, true, false

// --- Затем заголовки нашего проекта ---
#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h" // Для get_full_message_number
#include "../io/io_common.h"           // Для receive_protocol_message
#include "../io/io_interface.h"      // Для IOInterface, create_*_interface
#include "../config/config.h"        // Для AppConfig, load_config
#include "../utils/ts_queue.h"       // Для ThreadSafeQueue, queue_*
#include "svm_handlers.h"            // Для init_message_handlers, MessageHandler
#include "svm_timers.h"              // Для init/destroy мьютекса/cond, timer_thread_func, stop_timer_thread

// --- Глобальные переменные, разделяемые между потоками ---
// Определяем их здесь
ThreadSafeQueue *svm_incoming_queue = NULL;
ThreadSafeQueue *svm_outgoing_queue = NULL;
IOInterface *io_svm = NULL;
int global_client_handle = -1;
volatile bool keep_running = true; // Используется в обработчике сигнала и циклах потоков

// --- Прототипы функций потоков (реализации в отдельных файлах) ---
// Лучше включить соответствующие .h файлы, но пока можно и extern
extern void* receiver_thread_func(void* arg);
extern void* processor_thread_func(void* arg);
extern void* sender_thread_func(void* arg);
// timer_thread_func объявлен в svm_timers.h

// --- Обработчик сигналов для завершения ---
void handle_shutdown_signal(int sig) {
    // Используем write для signal-safety вместо printf/fprintf
    const char msg_int[] = "\nSVM: Получен сигнал SIGINT. Завершение...\n";
    const char msg_term[] = "\nSVM: Получен сигнал SIGTERM. Завершение...\n";
    const char msg_unknown[] = "\nSVM: Получен неизвестный сигнал. Завершение...\n";
    const char *msg_ptr;

    switch(sig) {
        case SIGINT: msg_ptr = msg_int; break;
        case SIGTERM: msg_ptr = msg_term; break;
        default: msg_ptr = msg_unknown; break;
    }
    write(STDOUT_FILENO, msg_ptr, strlen(msg_ptr)); // Пишем в stdout

    keep_running = false; // Устанавливаем флаг

    // НЕ вызываем здесь НЕ signal-safe функции (queue_shutdown, pthread_*)
    // Их вызовет main после выхода из ожидания потоков.
}


int main() {
	int serverSocketFD = -1; // Слушающий дескриптор
    AppConfig config;
    // Идентификаторы потоков
    pthread_t receiver_tid = 0, processor_tid = 0, sender_tid = 0, timer_tid = 0;
    // Флаги для корректной очистки
    bool timer_created = false, receiver_created = false, processor_created = false, sender_created = false;

	printf("SVM запуск...\n");

    // --- Инициализация ---
    if (init_svm_counters_mutex_and_cond() != 0) { // Инициализация мьютекса и cond таймера
        exit(EXIT_FAILURE);
    }
    init_message_handlers(); // Инициализация диспетчера сообщений

    // Загрузка конфигурации
    printf("SVM: Загрузка конфигурации...\n");
    if (load_config("config.ini", &config) != 0) {
        // Сообщение об ошибке уже выведено в load_config
        destroy_svm_counters_mutex_and_cond();
        exit(EXIT_FAILURE);
    }

    // Создание интерфейса IO
    printf("SVM: Создание интерфейса типа '%s'...\n", config.interface_type);
     if (strcasecmp(config.interface_type, "ethernet") == 0) {
        io_svm = create_ethernet_interface(&config.ethernet);
    } else if (strcasecmp(config.interface_type, "serial") == 0) {
        io_svm = create_serial_interface(&config.serial);
    } else {
         fprintf(stderr, "SVM: Неподдерживаемый тип интерфейса '%s' в config.ini.\n", config.interface_type);
         destroy_svm_counters_mutex_and_cond();
         exit(EXIT_FAILURE);
    }
    if (!io_svm) {
         fprintf(stderr, "SVM: Не удалось создать IOInterface.\n");
         destroy_svm_counters_mutex_and_cond();
         exit(EXIT_FAILURE);
    }

    // Создание очередей сообщений
    svm_incoming_queue = queue_create(100);
    svm_outgoing_queue = queue_create(100);
    if (!svm_incoming_queue || !svm_outgoing_queue) {
        fprintf(stderr, "SVM: Не удалось создать очереди сообщений.\n");
        goto cleanup; // Используем goto для централизованной очистки
    }

    // Установка обработчиков сигналов для грациозного завершения
    struct sigaction sa; // Теперь тип известен из <signal.h>
    memset(&sa, 0, sizeof(sa)); // memset теперь известен из <string.h>
    sa.sa_handler = handle_shutdown_signal;
    sigaction(SIGINT, &sa, NULL);  // SIGINT теперь известен из <signal.h>
    sigaction(SIGTERM, &sa, NULL); // SIGTERM теперь известен из <signal.h>

	// --- Сетевая/Портовая часть и принятие клиента ---
    if (io_svm->type == IO_TYPE_ETHERNET) {
        printf("SVM: Запуск прослушивания Ethernet...\n");
        serverSocketFD = io_svm->listen(io_svm); // Вызов listen через интерфейс
	    if (serverSocketFD < 0) {
            fprintf(stderr, "SVM: Ошибка запуска прослушивания Ethernet.\n");
            goto cleanup;
	    }
	    printf("SVM слушает на порту %d (listen handle: %d)\n", ((EthernetConfig*)io_svm->config)->port, serverSocketFD);

        char client_ip_str[INET_ADDRSTRLEN];
        uint16_t client_port_num;
        struct sockaddr_in clientAddress; // Нужно для accept, если он ее использует
        socklen_t clientAddressLength = sizeof(clientAddress); // Нужно для accept

        printf("SVM: Ожидание подключения UVM...\n");
        while(keep_running) { // Проверка флага перед вызовом accept
            // accept теперь часть интерфейса, передаем нужные параметры
            global_client_handle = io_svm->accept(io_svm, client_ip_str, sizeof(client_ip_str), &client_port_num);
            if (global_client_handle >= 0) {
                break; // Успешно приняли
            }
            if (errno == EINTR && keep_running) {
                printf("SVM: accept() прерван сигналом, повторная попытка...\n");
                continue;
            } else if (keep_running) {
                 perror("SVM: Ошибка принятия соединения");
                 goto cleanup;
            } else {
                printf("SVM: Ожидание соединения прервано или получен сигнал завершения.\n");
                goto cleanup;
            }
        }
         if (!keep_running) goto cleanup; // Если вышли из цикла по флагу

        if (serverSocketFD >= 0) {
             io_svm->disconnect(io_svm, serverSocketFD);
             printf("SVM: Слушающий сокет (handle: %d) закрыт.\n", serverSocketFD);
             serverSocketFD = -1;
        }

        printf("SVM принял соединение от UVM (%s:%u) (клиентский дескриптор: %d)\n",
               client_ip_str, client_port_num, global_client_handle);

    } else { // IO_TYPE_SERIAL
        printf("SVM: Открытие COM порта %s...\n", ((SerialConfig*)io_svm->config)->device); // Доступ к config
        global_client_handle = io_svm->connect(io_svm); // connect/listen открывают порт
        if (global_client_handle < 0) {
             fprintf(stderr, "SVM: Не удалось открыть COM порт %s.\n", ((SerialConfig*)io_svm->config)->device);
             goto cleanup;
        }
        printf("SVM: COM порт %s открыт (handle: %d)\n", ((SerialConfig*)io_svm->config)->device, global_client_handle);
    }

    // Проверка, что у нас есть валидный дескриптор клиента/порта
    if (global_client_handle < 0 && keep_running) {
         fprintf(stderr, "SVM: Не удалось установить коммуникационный канал.\n");
         goto cleanup;
    }

    // --- Запуск потоков ---
    printf("SVM: Запуск рабочих потоков...\n");
    // keep_running уже true

    if (pthread_create(&timer_tid, NULL, timer_thread_func, NULL) != 0) { perror("SVM: Failed to create timer thread"); goto cleanup_threads; }
    timer_created = true;
    if (pthread_create(&receiver_tid, NULL, receiver_thread_func, NULL) != 0) { perror("SVM: Failed to create receiver thread"); goto cleanup_threads; }
    receiver_created = true;
    if (pthread_create(&processor_tid, NULL, processor_thread_func, NULL) != 0) { perror("SVM: Failed to create processor thread"); goto cleanup_threads; }
    processor_created = true;
    if (pthread_create(&sender_tid, NULL, sender_thread_func, NULL) != 0) { perror("SVM: Failed to create sender thread"); goto cleanup_threads; }
    sender_created = true;

    printf("SVM: Все потоки запущены. Ожидание завершения (сигнал или закрытие UVM)...\n");

	// --- Ожидание завершения потоков ---
    if (receiver_created) pthread_join(receiver_tid, NULL);
    printf("SVM Main: Receiver thread joined.\n");

    if (keep_running) {
        printf("SVM Main: Receiver thread exited unexpectedly, initiating shutdown.\n");
        keep_running = false;
    } else {
        printf("SVM Main: Shutdown initiated (keep_running is false).\n");
    }

    printf("SVM Main: Signaling other threads to stop...\n");
    stop_timer_thread(); // Сигналим таймеру
    if (svm_incoming_queue) queue_shutdown(svm_incoming_queue);
    if (svm_outgoing_queue) queue_shutdown(svm_outgoing_queue);

    if (global_client_handle >= 0) {
        printf("SVM Main: Завершение клиентского соединения (handle: %d)...\n", global_client_handle);
        shutdown(global_client_handle, SHUT_RDWR); // Попытка разбудить блокирующие вызовы
    }

    // Ожидаем завершения остальных потоков
    if (processor_created) pthread_join(processor_tid, NULL);
    printf("SVM Main: Processor thread joined.\n");
    if (sender_created) pthread_join(sender_tid, NULL);
    printf("SVM Main: Sender thread joined.\n");
    if (timer_created) pthread_join(timer_tid, NULL);
    printf("SVM Main: Timer thread joined.\n");

    goto cleanup; // Переходим к общей очистке

cleanup_threads: // Сюда попадаем, если не удалось создать все потоки
    fprintf(stderr, "SVM: Ошибка создания одного из потоков. Инициируем остановку созданных...\n");
    keep_running = false; // Устанавливаем флаг для тех, что могли запуститься

    // Сигнализируем созданным потокам о завершении
    if (timer_created) stop_timer_thread();
    if (svm_incoming_queue) queue_shutdown(svm_incoming_queue);
    if (svm_outgoing_queue) queue_shutdown(svm_outgoing_queue);
     if (global_client_handle >= 0) shutdown(global_client_handle, SHUT_RDWR);

    // Ожидаем завершения уже созданных потоков (если они были созданы)
    if (timer_created && timer_tid != 0) pthread_join(timer_tid, NULL);
    if (receiver_created && receiver_tid != 0) pthread_join(receiver_tid, NULL);
    if (processor_created && processor_tid != 0) pthread_join(processor_tid, NULL);
    if (sender_created && sender_tid != 0) pthread_join(sender_tid, NULL);

cleanup:
	printf("SVM завершает работу и очищает ресурсы...\n");
    // Закрываем дескрипторы
    if (global_client_handle >= 0) {
        if (io_svm) io_svm->disconnect(io_svm, global_client_handle);
        else close(global_client_handle); // На всякий случай
        global_client_handle = -1; // Сбрасываем
        printf("SVM: Клиентский handle закрыт.\n");
    }
    if (serverSocketFD >= 0) { // Закрываем слушающий сокет, если он остался
         if (io_svm) io_svm->disconnect(io_svm, serverSocketFD);
         else close(serverSocketFD);
         printf("SVM: Слушающий handle закрыт.\n");
    }
    // Уничтожаем очереди
    if (svm_incoming_queue) queue_destroy(svm_incoming_queue);
    if (svm_outgoing_queue) queue_destroy(svm_outgoing_queue);
    // Уничтожаем интерфейс
    if (io_svm) io_svm->destroy(io_svm);
    // Уничтожаем мьютекс и cond var таймера
    destroy_svm_counters_mutex_and_cond();

    printf("SVM: Очистка завершена.\n");
	return 0;
}