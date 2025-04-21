/*
 * svm/svm_main.c
 * Описание: Основной файл SVM: инициализация, управление МНОЖЕСТВОМ экземпляров СВ-М,
 * создание потоков (ОБЩИЕ Timer/Sender, ПЕРСОНАЛЬНЫЕ Listener/Receiver/Processor),
 * управление их жизненным циклом. Использует подход "1 поток accept на порт".
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
#include "../utils/ts_queued_msg_queue.h" // Очередь для QueuedMessage
#include "svm_handlers.h"
#include "svm_timers.h"
#include "svm_types.h"

// --- Глобальные переменные ---
AppConfig config;
SvmInstance svm_instances[MAX_SVM_CONFIGS]; // Используем MAX_SVM_CONFIGS из config.h
ThreadSafeQueuedMsgQueue *svm_outgoing_queue = NULL;
// IOInterface *io_svm = NULL; // IO интерфейс теперь создается для каждого listener'а
pthread_mutex_t svm_instances_mutex; // Мьютекс для ЗАЩИТЫ МАССИВА (например, при поиске)
// int listen_socket_fd = -1; // Больше не один слушающий сокет
int listen_sockets[MAX_SVM_CONFIGS]; // Массив слушающих сокетов
pthread_t listener_threads[MAX_SVM_CONFIGS]; // Массив потоков-слушателей

volatile bool keep_running = true;

// --- Прототипы потоков ---
extern void* receiver_thread_func(void* arg);
extern void* processor_thread_func(void* arg);
extern void* sender_thread_func(void* arg);
extern void* timer_thread_func(void* arg);
void* listener_thread_func(void* arg); // Новый поток-слушатель

// --- Обработчик сигналов ---
void handle_shutdown_signal(int sig) {
    (void)sig;
    const char msg[] = "\nSVM: Received shutdown signal. Shutting down all listeners and instances...\n";
    ssize_t written __attribute__((unused)) = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    keep_running = false;

    // Закрываем ВСЕ слушающие сокеты, чтобы разбудить потоки listener_thread_func
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        int fd = listen_sockets[i];
        if (fd >= 0) {
            listen_sockets[i] = -1; // Предотвращаем повторное закрытие
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
    }
    // Также нужно разбудить Processor'ы и Sender'а
    if (svm_outgoing_queue) qmq_shutdown(svm_outgoing_queue);
    // Разбудить Processor'ы сложнее, т.к. нужно пройти по всем активным instance->incoming_queue
    // Это сделаем в основном потоке при завершении
    stop_timer_thread_signal(); // Сигналим таймеру
}

// --- Функция инициализации экземпляра (без изменений) ---
void initialize_svm_instance(SvmInstance *instance, int id) {
    if (!instance) return;
    memset(instance, 0, sizeof(SvmInstance));
    instance->id = id;
    instance->client_handle = -1;
    instance->is_active = false;
    instance->incoming_queue = NULL;
    instance->receiver_tid = 0;
    instance->processor_tid = 0;
    instance->current_state = STATE_NOT_INITIALIZED;
    instance->message_counter = 0;
    instance->bcb_counter = 0;
    instance->link_up_changes_counter = 0;
    instance->link_up_low_time_us100 = 0;
    instance->sign_det_changes_counter = 0;
    instance->link_status_timer_counter = 0;
    // Инициализация мьютекса происходит один раз в main
    // если pthread_mutex_init вызывается здесь, то нужен destroy при удалении
    // Оставляем инициализацию мьютекса в main
    // if (pthread_mutex_init(&instance->instance_mutex, NULL) != 0) {
    //      perror("Failed to initialize instance mutex");
    //      exit(EXIT_FAILURE);
    // }
}

// --- Поток-слушатель для одного порта/экземпляра ---
typedef struct {
    int svm_id;
    int listen_fd;
} ListenerArgs;

void* listener_thread_func(void* arg) {
    ListenerArgs *args = (ListenerArgs*)arg;
    int svm_id = args->svm_id;
    int lfd = args->listen_fd;
    free(args); // Освобождаем память из main

    if (lfd < 0) {
        fprintf(stderr, "Listener (SVM %d): Invalid listen socket provided.\n", svm_id);
        return NULL;
    }

    SvmInstance *instance = &svm_instances[svm_id]; // Указатель на наш экземпляр
    printf("Listener thread started for SVM ID %d (LAK 0x%02X, Port %d, Listen FD %d)\n",
           svm_id, instance->assigned_lak, config.svm_ethernet[svm_id].port, lfd);


    while (keep_running) {
        char client_ip_str[INET_ADDRSTRLEN];
        uint16_t client_port_num;
        struct sockaddr_in client_addr; // Нужен для accept
        socklen_t client_len = sizeof(client_addr);
        int client_handle = -1;

        printf("Listener (SVM %d): Waiting for connection on port %d...\n", svm_id, config.svm_ethernet[svm_id].port);
        client_handle = accept(lfd, (struct sockaddr *)&client_addr, &client_len);

        if (client_handle < 0) {
            if (!keep_running || errno == EBADF) { // Прервано сигналом или сокет закрыт
                printf("Listener (SVM %d): Accept loop interrupted or socket closed.\n", svm_id);
            } else if (errno == EINTR) {
                printf("Listener (SVM %d): accept() interrupted, retrying...\n", svm_id);
                continue;
            } else {
                 if (keep_running) perror("Listener accept failed");
            }
            break; // Выходим из цикла при ошибке или завершении
        }

        // Соединение принято!
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_str, sizeof(client_ip_str));
        client_port_num = ntohs(client_addr.sin_port);
        printf("Listener (SVM %d): Accepted connection from %s:%u (Client FD %d)\n",
               svm_id, client_ip_str, client_port_num, client_handle);

        // Захватываем мьютекс экземпляра для его настройки
        pthread_mutex_lock(&instance->instance_mutex);
        if (instance->is_active) {
             pthread_mutex_unlock(&instance->instance_mutex);
             fprintf(stderr, "Listener (SVM %d): Instance is already active! Rejecting new connection.\n", svm_id);
             close(client_handle);
             continue; // Ждем следующего соединения (хотя это странная ситуация)
        }

        // Настраиваем экземпляр
        instance->client_handle = client_handle;
        // instance->io_handle теперь не нужен глобально или в instance, т.к. функции send/recv вызываются с хэндлом
        instance->current_state = STATE_NOT_INITIALIZED;
        instance->message_counter = 0;
        instance->bcb_counter = 0; // Сброс счетчиков
        instance->link_up_changes_counter = 0;
        instance->link_up_low_time_us100 = 0;
        instance->sign_det_changes_counter = 0;
        instance->link_status_timer_counter = 0;

        // Создаем входящую очередь
        instance->incoming_queue = qmq_create(100);
        if (!instance->incoming_queue) {
             pthread_mutex_unlock(&instance->instance_mutex);
             fprintf(stderr, "Listener (SVM %d): Failed to create incoming queue. Rejecting.\n", svm_id);
             close(client_handle);
             continue;
        }

        // Запускаем рабочие потоки
        bool receiver_ok = false, processor_ok = false;
        if (pthread_create(&instance->receiver_tid, NULL, receiver_thread_func, instance) == 0) {
            receiver_ok = true;
            if (pthread_create(&instance->processor_tid, NULL, processor_thread_func, instance) == 0) {
                processor_ok = true;
            } else {
                perror("Listener: Failed to create processor thread");
                pthread_cancel(instance->receiver_tid); // Отменяем ресивер
                pthread_join(instance->receiver_tid, NULL);
            }
        } else {
             perror("Listener: Failed to create receiver thread");
        }

        if (receiver_ok && processor_ok) {
            instance->is_active = true; // Помечаем активным
            printf("Listener (SVM %d): Instance activated. Worker threads started.\n", svm_id);
            pthread_mutex_unlock(&instance->instance_mutex); // Отпускаем мьютекс экземпляра

            // --- Ожидание завершения рабочих потоков ---
            printf("Listener (SVM %d): Waiting for worker threads to finish...\n", svm_id);
            pthread_join(instance->receiver_tid, NULL);
            printf("Listener (SVM %d): Receiver thread joined.\n", svm_id);
            pthread_join(instance->processor_tid, NULL);
            printf("Listener (SVM %d): Processor thread joined.\n", svm_id);

            // --- Очистка после завершения клиента ---
             printf("Listener (SVM %d): Worker threads finished. Cleaning up instance...\n", svm_id);
             pthread_mutex_lock(&instance->instance_mutex);
             if (instance->client_handle >= 0) {
                 close(instance->client_handle); // Закрываем сокет клиента
                 instance->client_handle = -1;
             }
             if (instance->incoming_queue) {
                 qmq_destroy(instance->incoming_queue);
                 instance->incoming_queue = NULL;
             }
             instance->is_active = false;
             instance->receiver_tid = 0;
             instance->processor_tid = 0;
             pthread_mutex_unlock(&instance->instance_mutex);
             printf("Listener (SVM %d): Instance deactivated. Ready for new connection.\n", svm_id);

        } else {
            // Не удалось запустить потоки
            pthread_mutex_unlock(&instance->instance_mutex);
            fprintf(stderr, "Listener (SVM %d): Failed to start worker threads. Rejecting.\n", svm_id);
            if(instance->incoming_queue) qmq_destroy(instance->incoming_queue);
            instance->incoming_queue = NULL;
            close(client_handle);
            continue; // Возвращаемся к accept
        }

    } // end while(keep_running)

    printf("Listener thread for SVM ID %d finished.\n", svm_id);
    // Закрываем слушающий сокет при выходе (если он еще не закрыт)
    if (lfd >= 0) {
         close(lfd);
         // Сбрасываем глобальный дескриптор (нужна защита мьютексом, если main может его использовать)
         // pthread_mutex_lock(&some_global_mutex); listen_sockets[svm_id] = -1; pthread_mutex_unlock(..);
    }
    return NULL;
}


// Основная функция
int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
    // int svm_id = 0; // svm_id больше не нужен здесь

    pthread_t timer_tid = 0, sender_tid = 0;
    bool threads_started = false;

    printf("SVM Multi-Port Server starting...\n");

    // Инициализация мьютекса массива и таймера
    if (pthread_mutex_init(&svm_instances_mutex, NULL) != 0) { /*...*/ exit(EXIT_FAILURE); }
    if (init_svm_timer_sync() != 0) { /*...*/ exit(EXIT_FAILURE); }
    init_message_handlers();

    // Инициализация всех экземпляров и слушающих сокетов
	for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
		initialize_svm_instance(&svm_instances[i], i);
		// Инициализируем мьютекс экземпляра ЗДЕСЬ
		if (pthread_mutex_init(&svm_instances[i].instance_mutex, NULL) != 0) {
			perror("Failed to initialize instance mutex");
			// Очистка уже созданных мьютексов перед выходом
			for (int j = 0; j < i; ++j) {
				pthread_mutex_destroy(&svm_instances[j].instance_mutex);
			}
			exit(EXIT_FAILURE);
		}
		listen_sockets[i] = -1;
		listener_threads[i] = 0;
	}

    // Загрузка конфигурации (читает все секции)
    printf("SVM: Loading configuration...\n");
    if (load_config("config.ini", &config) != 0) {
        goto cleanup_sync;
    }
    int num_svms_to_run = config.num_svm_configs_found;
    if (num_svms_to_run == 0) { /*...*/ goto cleanup_sync; }
    printf("SVM: Will attempt to start %d instances based on config.\n", num_svms_to_run);


    // Создание общей исходящей очереди
    svm_outgoing_queue = qmq_create(100 * num_svms_to_run);
    if (!svm_outgoing_queue) { /*...*/ goto cleanup_sync; }

    // Установка сигналов
    signal(SIGINT, handle_shutdown_signal);
    signal(SIGTERM, handle_shutdown_signal);

    // --- Создание и запуск потоков-слушателей ---
    int listeners_started = 0;
    for (int i = 0; i < num_svms_to_run; ++i) {
        if (!config.svm_config_loaded[i]) continue; // Пропускаем, если конфига нет

        // Устанавливаем LAK для экземпляра ДО запуска потока
        svm_instances[i].assigned_lak = config.svm_settings[i].lak;

        // Создаем IO интерфейс ТОЛЬКО для получения порта из конфига
        // Временное решение: создаем полный интерфейс, чтобы вызвать listen
        EthernetConfig listen_config;
        memset(&listen_config, 0, sizeof(listen_config));
        listen_config.port = config.svm_ethernet[i].port;
        IOInterface* temp_io = create_ethernet_interface(&listen_config);
        if (!temp_io) {
             fprintf(stderr, "SVM: Failed to create temp IO interface for instance %d\n", i);
             continue; // Пропускаем этот экземпляр
        }

        // Начинаем слушать порт
        listen_sockets[i] = temp_io->listen(temp_io);
        temp_io->destroy(temp_io); // Уничтожаем временный интерфейс

        if (listen_sockets[i] < 0) {
            fprintf(stderr, "SVM: Failed to listen on port %d for instance %d. Skipping.\n",
                    config.svm_ethernet[i].port, i);
            continue; // Пропускаем этот экземпляр
        }

        // Готовим аргументы для потока-слушателя
        ListenerArgs *args = malloc(sizeof(ListenerArgs));
        if (!args) {
             perror("SVM: Failed to allocate listener args");
             close(listen_sockets[i]);
             listen_sockets[i] = -1;
             continue;
        }
        args->svm_id = i;
        args->listen_fd = listen_sockets[i];

        // Запускаем поток-слушатель
        if (pthread_create(&listener_threads[i], NULL, listener_thread_func, args) != 0) {
            perror("SVM: Failed to create listener thread");
            close(listen_sockets[i]);
            listen_sockets[i] = -1;
            free(args);
            // TODO: Более чистая остановка уже запущенных listener'ов?
            continue;
        }
        listeners_started++;
    } // end for start listeners

    if (listeners_started == 0) {
        fprintf(stderr, "SVM: Failed to start any listeners. Exiting.\n");
        goto cleanup_queues;
    }

    // --- Запуск общих потоков ---
    printf("SVM: Starting common threads (Timer, Sender)...\n");
    if (pthread_create(&timer_tid, NULL, timer_thread_func, svm_instances) != 0) { /*...*/ goto cleanup_listeners; }
    if (pthread_create(&sender_tid, NULL, sender_thread_func, NULL) != 0) { /*...*/ goto cleanup_timer; }
    threads_started = true;
    printf("SVM: All common threads started. %d listeners active. Running...\n", listeners_started);

    // --- Ожидание завершения ---
    // Главный поток просто ждет сигнала завершения или ошибки
    // Ожидание потоков-слушателей будет в секции cleanup
    while(keep_running) {
        sleep(1); // Просто спим, основная работа в других потоках
    }

    printf("SVM Main: Shutdown initiated. Waiting for threads to join...\n");

    // --- Ожидание завершения и очистка ---

cleanup_timer:
    if (threads_started && timer_tid != 0) {
        stop_timer_thread_signal();
        pthread_join(timer_tid, NULL);
        printf("SVM Main: Timer thread joined.\n");
    }
cleanup_listeners: // Сюда приходим, если не удалось запустить Sender или Timer
    // Сигналим и ждем завершения потоков-слушателей
    keep_running = false; // Убедимся, что флаг установлен
    // Обработчик сигнала уже закрыл сокеты, потоки должны выйти из accept
    printf("SVM Main: Waiting for listener threads to join...\n");
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        if (listener_threads[i] != 0) {
            pthread_join(listener_threads[i], NULL);
             printf("SVM Main: Listener thread for SVM ID %d joined.\n", i);
        }
         // Рабочие потоки экземпляров (Receiver/Processor) завершатся и будут собраны внутри listener_thread_func
    }
    printf("SVM Main: All listener threads joined.\n");

    // Ждем общий Sender поток (если он был запущен)
    if (threads_started && sender_tid != 0) {
        // Sender завершится, когда его очередь станет пустой и shutdown=true
         if (svm_outgoing_queue) qmq_shutdown(svm_outgoing_queue); // На всякий случай
        pthread_join(sender_tid, NULL);
        printf("SVM Main: Sender thread joined.\n");
    }

cleanup_queues:
	printf("SVM: Cleaning up queues...\n");
    if (svm_outgoing_queue) qmq_destroy(svm_outgoing_queue);
    // Входящие очереди уничтожаются внутри listener_thread_func

// cleanup_io: // IO интерфейсы создавались и уничтожались локально
//     printf("SVM: Cleaning up IO interface...\n");
//     // Слушающие сокеты уже закрыты в обработчике или listener'ах

cleanup_sync:
    printf("SVM: Cleaning up synchronization primitives...\n");
    destroy_svm_timer_sync();
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        pthread_mutex_destroy(&svm_instances[i].instance_mutex); // <-- Уничтожаем мьютекс
    }
    pthread_mutex_destroy(&svm_instances_mutex);

    printf("SVM: Cleanup finished. Exiting.\n");
	return 0;
}