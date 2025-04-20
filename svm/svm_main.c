/*
 * svm/svm_main.c
 * Описание: Основной файл SVM: инициализация, создание потоков,
 * управление жизненным циклом для ОДНОГО экземпляра SVM.
 * Параметры (порт, LAK) берутся из config.ini на основе ID.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>

#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../io/io_common.h"
#include "../io/io_interface.h"
#include "../config/config.h"
#include "../utils/ts_queue.h" // Обычная очередь для Message
#include "svm_handlers.h"
#include "svm_timers.h"

// --- Глобальные переменные (одно-экземплярная модель) ---
ThreadSafeQueue *svm_incoming_queue = NULL;
ThreadSafeQueue *svm_outgoing_queue = NULL;
IOInterface *io_svm = NULL;
int global_client_handle = -1;
int listen_socket_fd = -1; // Слушающий сокет
volatile bool keep_running = true;
extern LogicalAddress svm_logical_address; // LAK этого экземпляра (определен в svm_handlers.c)

// --- Прототипы потоков ---
extern void* receiver_thread_func(void* arg);
extern void* processor_thread_func(void* arg);
extern void* sender_thread_func(void* arg);
extern void* timer_thread_func(void* arg);

// --- Обработчик сигналов ---
void handle_shutdown_signal(int sig) {
    const char msg_int[] = "\nSVM: Received SIGINT. Shutting down...\n";
    const char msg_term[] = "\nSVM: Received SIGTERM. Shutting down...\n";
    const char *msg_ptr = (sig == SIGINT) ? msg_int : msg_term;
    ssize_t written __attribute__((unused)) = write(STDOUT_FILENO, msg_ptr, strlen(msg_ptr));
    keep_running = false;
    // Закрываем слушающий сокет, чтобы разбудить accept
    if (listen_socket_fd >= 0) {
        int fd = listen_socket_fd;
        listen_socket_fd = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
    // Закрываем клиентский сокет, чтобы разбудить receiver/sender
    if (global_client_handle >= 0) {
        int fd = global_client_handle;
        global_client_handle = -1;
        shutdown(fd, SHUT_RDWR);
        // close(fd); // Закроем в main после join
    }
}

int main(int argc, char *argv[]) {
    AppConfig config;
    pthread_t receiver_tid = 0, processor_tid = 0, sender_tid = 0, timer_tid = 0;
    bool threads_started = false;
    int svm_id = 0; // ID экземпляра по умолчанию

    // --- Парсинг аргумента командной строки (ID экземпляра) ---
    if (argc > 1) {
        svm_id = atoi(argv[1]);
        if (svm_id < 0 || svm_id >= 4) { // Предполагаем максимум 4 экземпляра для конфига
            fprintf(stderr, "Usage: %s [svm_id (0-3)]\n", argv[0]);
            exit(EXIT_FAILURE);
        }
        printf("SVM Instance ID: %d\n", svm_id);
    } else {
        printf("SVM Instance ID: %d (default)\n", svm_id);
    }

    // --- Инициализация ---
    if (init_svm_counters_mutex_and_cond() != 0) { exit(EXIT_FAILURE); }
    init_message_handlers();

	printf("SVM (ID %d): Loading configuration...\n", svm_id);
	// Загружаем ВСЮ конфигурацию
	if (load_config("config.ini", &config) != 0) {
		destroy_svm_counters_mutex_and_cond();
		exit(EXIT_FAILURE);
	}
	// Проверяем, был ли конфиг для нашего ID загружен
	if (svm_id < 0 || svm_id >= MAX_SVM_CONFIGS || !config.svm_config_loaded[svm_id]) {
		fprintf(stderr, "SVM (ID %d): Error: Configuration section not found or invalid ID.\n", svm_id);
		destroy_svm_counters_mutex_and_cond();
		exit(EXIT_FAILURE);
	}
	// Используем параметры из нужного индекса массива
	svm_logical_address = config.svm_settings[svm_id].lak;
	uint16_t listen_port = config.svm_ethernet[svm_id].port;

    // Создание IO интерфейса
    printf("SVM: Creating IO interface type '%s' for port %u, LAK 0x%02X...\n",
           config.interface_type, listen_port, svm_logical_address);
    if (strcasecmp(config.interface_type, "ethernet") == 0) {
    // Создаем временную копию EthernetConfig с нужным портом
    EthernetConfig instance_eth_config = config.uvm_ethernet_target; // Берем базовую (хотя она для UVM)
    instance_eth_config.port = listen_port; // Устанавливаем порт для этого SVM
    // strcpy(instance_eth_config.target_ip, "0.0.0.0"); // SVM должен слушать на всех IP
    io_svm = create_ethernet_interface(&instance_eth_config);
	} else {
        fprintf(stderr, "SVM: Only ethernet interface type is supported.\n");
        destroy_svm_counters_mutex_and_cond();
        exit(EXIT_FAILURE);
    }
    if (!io_svm) { /* ... */ destroy_svm_counters_mutex_and_cond(); exit(EXIT_FAILURE); }

    // Создание очередей
    svm_incoming_queue = queue_create(100);
    svm_outgoing_queue = queue_create(100);
    if (!svm_incoming_queue || !svm_outgoing_queue) { /* ... */ goto cleanup_io; }

    // Установка сигналов
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_shutdown_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // --- Сетевая часть ---
    if (io_svm->type == IO_TYPE_ETHERNET) {
        printf("SVM (LAK 0x%02X): Starting listener on port %u...\n", svm_logical_address, listen_port);
        listen_socket_fd = io_svm->listen(io_svm); // listen использует порт из config.ethernet
        if (listen_socket_fd < 0) { /* ... */ goto cleanup_queues; }
        printf("SVM (LAK 0x%02X) listening (handle: %d). Waiting for 1 UVM connection...\n", svm_logical_address, listen_socket_fd);

        while (keep_running) { // Цикл ожидания ОДНОГО соединения
            char client_ip_str[INET_ADDRSTRLEN];
            uint16_t client_port_num;
            int lfd = listen_socket_fd;
            if (lfd < 0) break; // Сокет закрыт сигналом

            global_client_handle = io_svm->accept(io_svm, client_ip_str, sizeof(client_ip_str), &client_port_num);

            if (global_client_handle >= 0) {
                printf("SVM (LAK 0x%02X) accepted connection from %s:%u (client handle: %d)\n",
                       svm_logical_address, client_ip_str, client_port_num, global_client_handle);
                // Закрываем слушающий сокет, так как мы обрабатываем только одного клиента
                if (listen_socket_fd >= 0) {
                    io_svm->disconnect(io_svm, listen_socket_fd);
                    listen_socket_fd = -1;
                }
                break; // Выходим из цикла ожидания соединения
            } else {
                if (!keep_running || errno == EBADF) {
                    printf("SVM: Listener interrupted or socket closed while waiting for connection.\n");
                } else if (errno == EINTR) {
                    printf("SVM: accept() interrupted, retrying...\n");
                    continue;
                } else {
                    if (keep_running) perror("SVM: Error accepting connection");
                }
                goto cleanup_queues; // Ошибка или завершение до соединения
            }
        } // end while accept

        // Проверяем, установили ли соединение перед запуском потоков
        if (global_client_handle < 0) {
             if (keep_running) fprintf(stderr,"SVM: Failed to accept connection.\n");
             goto cleanup_queues;
        }

    } else { /* ... Serial not supported ... */ }

    // --- Запуск потоков ---
    printf("SVM (LAK 0x%02X): Starting worker threads...\n", svm_logical_address);
    if (pthread_create(&timer_tid, NULL, timer_thread_func, NULL) != 0) { /*...*/ goto cleanup_connection; }
    if (pthread_create(&receiver_tid, NULL, receiver_thread_func, NULL) != 0) { /*...*/ goto cleanup_timer; }
    if (pthread_create(&processor_tid, NULL, processor_thread_func, NULL) != 0) { /*...*/ goto cleanup_receiver; }
    if (pthread_create(&sender_tid, NULL, sender_thread_func, NULL) != 0) { /*...*/ goto cleanup_processor; }
    threads_started = true;
    printf("SVM (LAK 0x%02X): All threads started. Running...\n", svm_logical_address);

    // --- Ожидание завершения ---
    // Главный поток просто ждет завершения потока Receiver
    // Receiver завершится при ошибке, закрытии соединения или сигнале keep_running = false
    if (receiver_tid != 0) {
        pthread_join(receiver_tid, NULL);
        printf("SVM Main: Receiver thread joined.\n");
    }

    // Если Receiver завершился, а мы еще работаем, инициируем остановку
    if (keep_running) {
        printf("SVM Main: Receiver exited unexpectedly. Initiating shutdown...\n");
        keep_running = false;
         // Сигналим остальным потокам
         if (svm_incoming_queue) queue_shutdown(svm_incoming_queue);
         if (svm_outgoing_queue) queue_shutdown(svm_outgoing_queue);
         stop_timer_thread();
         if (global_client_handle >= 0) shutdown(global_client_handle, SHUT_RDWR);
    }

    // --- Ожидание остальных потоков ---
    printf("SVM Main: Joining remaining threads...\n");
    if (processor_tid != 0) pthread_join(processor_tid, NULL);
    printf("SVM Main: Processor thread joined.\n");
    if (sender_tid != 0) pthread_join(sender_tid, NULL);
    printf("SVM Main: Sender thread joined.\n");
    if (timer_tid != 0) pthread_join(timer_tid, NULL);
    printf("SVM Main: Timer thread joined.\n");

    goto cleanup_connection; // Переходим к очистке

// Метки для корректной очистки при ошибках на разных этапах
cleanup_processor:
    if (threads_started) { keep_running = false; pthread_cancel(processor_tid); pthread_join(processor_tid, NULL); }
cleanup_receiver:
    if (threads_started) { pthread_cancel(receiver_tid); pthread_join(receiver_tid, NULL); }
cleanup_timer:
    if (threads_started) { stop_timer_thread(); pthread_join(timer_tid, NULL); }
cleanup_connection:
    if (global_client_handle >= 0) {
        if (io_svm) io_svm->disconnect(io_svm, global_client_handle);
        else close(global_client_handle);
        global_client_handle = -1;
        printf("SVM: Client handle closed.\n");
    }
cleanup_queues:
    if (svm_incoming_queue) queue_destroy(svm_incoming_queue);
    if (svm_outgoing_queue) queue_destroy(svm_outgoing_queue);
cleanup_io:
    if (listen_socket_fd >= 0) { // Закрываем слушающий сокет, если остался
         if (io_svm) io_svm->disconnect(io_svm, listen_socket_fd);
         else close(listen_socket_fd);
    }
    if (io_svm) io_svm->destroy(io_svm);
cleanup_sync:
    destroy_svm_counters_mutex_and_cond();

    printf("SVM (ID %d) finished.\n", svm_id);
    return 0;
}