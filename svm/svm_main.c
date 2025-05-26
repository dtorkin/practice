/*
 * svm/svm_main.c
 * Описание: Основной файл SVM: инициализация, управление МНОЖЕСТВОМ экземпляров СВ-М,
 * создание потоков (ОБЩИЙ Sender, ПЕРСОНАЛЬНЫЕ Listener/Receiver/Processor/Timer),
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

// --- Заголовки проекта ---
#include "../config/config.h"
#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../io/io_common.h"
#include "../io/io_interface.h"
#include "../utils/ts_queued_msg_queue.h"
#include "svm_handlers.h"
#include "svm_timers.h" // Содержит объявления get_instance_..._counter и svm_instance_timer_thread_func
#include "svm_types.h"

// --- Глобальные переменные ---
AppConfig config;
SvmInstance svm_instances[MAX_SVM_INSTANCES];
ThreadSafeQueuedMsgQueue *svm_outgoing_queue = NULL;
pthread_mutex_t svm_instances_mutex; // Глобальный мьютекс для всего массива svm_instances, если он нужен.
                                      // Пока что каждый instance имеет свой мьютекс.
int listen_sockets[MAX_SVM_INSTANCES];
pthread_t listener_threads[MAX_SVM_INSTANCES];

volatile bool keep_running = true; // Общий флаг работы для всего svm_app

// --- Прототипы потоков ---
extern void* receiver_thread_func(void* arg);
extern void* processor_thread_func(void* arg);
extern void* sender_thread_func(void* arg);
// extern void* timer_thread_func(void* arg); // Общий таймер УДАЛЕН
extern void* svm_instance_timer_thread_func(void* arg); // Персональный таймер ОБЪЯВЛЕН в svm_timers.h
void* listener_thread_func(void* arg);

// --- Обработчик сигналов ---
void handle_shutdown_signal(int sig) {
    (void)sig;
    const char msg[] = "\nSVM: Received shutdown signal. Shutting down all listeners and instances...\n";
    ssize_t written __attribute__((unused)) = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    keep_running = false; // Главный флаг для всех потоков

    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        int fd = listen_sockets[i];
        if (fd >= 0) {
            listen_sockets[i] = -1;
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
        // Сигнализируем персональным таймерам через их флаги (если они уже запущены)
        // Это будет сделано в listener_thread_func при его завершении или здесь, если необходимо экстренно
        // pthread_mutex_lock(&svm_instances[i].instance_mutex);
        // svm_instances[i].personal_timer_keep_running = false;
        // pthread_mutex_unlock(&svm_instances[i].instance_mutex);
    }

    if (svm_outgoing_queue) qmq_shutdown(svm_outgoing_queue);
    // stop_timer_thread_signal(); // Общего таймера больше нет
}

// --- Функция инициализации экземпляра ---
void initialize_svm_instance(SvmInstance *instance, int id, LogicalAddress lak_from_config, const SvmInstanceSettings* settings_from_config) {
    if (!instance || !settings_from_config) return;
    memset(instance, 0, sizeof(SvmInstance));
    instance->id = id;
    instance->client_handle = -1;
    instance->is_active = false; // Станет true, когда все потоки экземпляра запущены
    instance->incoming_queue = NULL;
    instance->receiver_tid = 0;
    instance->processor_tid = 0;
    instance->timer_tid = 0; // Инициализация ID персонального таймера
    instance->personal_timer_keep_running = false; // Инициализация флага персонального таймера

    instance->current_state = STATE_NOT_INITIALIZED;
    instance->message_counter = 0;
    instance->messages_sent_count = 0;

    instance->bcb_counter = 0;
    instance->link_up_changes_counter = 0;
    instance->link_up_low_time_us100 = 0;
    instance->sign_det_changes_counter = 0;
    instance->link_status_timer_counter = 0; // Переименованное поле

    instance->assigned_lak = lak_from_config;
	instance->user_flag1 = false;
    instance->simulate_control_failure = settings_from_config->simulate_control_failure;
    instance->disconnect_after_messages = settings_from_config->disconnect_after_messages;
    instance->simulate_response_timeout = settings_from_config->simulate_response_timeout;
    instance->send_warning_on_confirm = settings_from_config->send_warning_on_confirm;
    instance->warning_tks = settings_from_config->warning_tks;
    // Мьютекс instance->instance_mutex инициализируется в main()
}

// --- Поток-слушатель для одного порта/экземпляра ---
typedef struct {
    int svm_id;
    uint16_t port;
    LogicalAddress lak;
} ListenerArgs;

void* listener_thread_func(void* arg) {
    ListenerArgs *args = (ListenerArgs*)arg;
    int svm_id = args->svm_id;
    uint16_t port = args->port;
    LogicalAddress lak = args->lak;
    free(args);

    IOInterface *listener_io = NULL;
    int lfd = -1;
    SvmInstance *instance = &svm_instances[svm_id];
    // instance->assigned_lak = lak; // LAK уже установлен в initialize_svm_instance, который вызовет main

    EthernetConfig listen_config = {0};
    listen_config.port = port; // Используем порт из аргументов
    listener_io = create_ethernet_interface(&listen_config);
    if (!listener_io) {
        fprintf(stderr, "Listener (SVM %d, Port %u): Failed to create IO interface.\n", svm_id, port);
        return NULL;
    }

    lfd = listener_io->listen(listener_io);
    if (lfd < 0) {
        fprintf(stderr, "Listener (SVM %d, Port %u): Failed to listen.\n", svm_id, port);
        listener_io->destroy(listener_io);
        return NULL;
    }
    listen_sockets[svm_id] = lfd;

    printf("Listener thread started for SVM ID %d (LAK 0x%02X, Port %u, Listen FD %d)\n", svm_id, instance->assigned_lak, port, lfd);

    while (keep_running) {
        char client_ip_str[INET_ADDRSTRLEN];
        uint16_t client_port_num;
        int client_handle = -1;

        printf("Listener (SVM %d, Port %u): Waiting for connection...\n", svm_id, port);
        client_handle = listener_io->accept(listener_io, client_ip_str, sizeof(client_ip_str), &client_port_num);

        if (client_handle < 0) {
            if (!keep_running || errno == EBADF) {
                 printf("Listener (SVM %d, Port %u): Accept loop interrupted or socket closed.\n", svm_id, port);
            } else if (errno == EINTR) { continue; }
            else { if(keep_running) perror("Listener accept failed"); }
            break;
        }
        printf("Listener (SVM %d, Port %u): Accepted connection from %s:%u (Client FD %d)\n",
               svm_id, port, client_ip_str, client_port_num, client_handle);

        pthread_mutex_lock(&instance->instance_mutex);
        if (instance->is_active) {
             pthread_mutex_unlock(&instance->instance_mutex);
             fprintf(stderr, "Listener (SVM %d, Port %u): Instance is already active! Rejecting new connection.\n", svm_id, port);
             close(client_handle);
             continue;
        }

        instance->client_handle = client_handle;
        instance->io_handle = listener_io; // Сохраняем указатель на IO для этого клиента
        instance->current_state = STATE_NOT_INITIALIZED;
        instance->message_counter = 0;
        instance->messages_sent_count = 0; // Сброс счетчика для имитации disconnect_after_messages
        instance->bcb_counter = 0;
        instance->link_up_changes_counter = 0;
        instance->link_up_low_time_us100 = 0;
        instance->sign_det_changes_counter = 0;
        instance->link_status_timer_counter = 0;
        instance->user_flag1 = false; // Сброс флагов имитации

        instance->incoming_queue = qmq_create(100);
        if (!instance->incoming_queue) {
             pthread_mutex_unlock(&instance->instance_mutex);
             fprintf(stderr, "Listener (SVM %d, Port %u): Failed to create incoming queue. Rejecting.\n", svm_id, port);
             close(client_handle);
             instance->io_handle = NULL;
             continue;
        }

        bool receiver_ok = false, processor_ok = false, timer_ok = false;
        instance->receiver_tid = 0; instance->processor_tid = 0; instance->timer_tid = 0;
        instance->personal_timer_keep_running = true; // Готовим флаг для таймера

        if (pthread_create(&instance->receiver_tid, NULL, receiver_thread_func, instance) == 0) {
            receiver_ok = true;
            if (pthread_create(&instance->processor_tid, NULL, processor_thread_func, instance) == 0) {
                processor_ok = true;
                if (pthread_create(&instance->timer_tid, NULL, svm_instance_timer_thread_func, instance) == 0) {
                    timer_ok = true;
                } else {
                    perror("Listener: Failed to create instance timer thread");
                    instance->personal_timer_keep_running = false; // Отменить, если таймер не стартанул
                    // Отменяем processor и receiver
                    if(processor_ok) { pthread_cancel(instance->processor_tid); pthread_join(instance->processor_tid, NULL); instance->processor_tid = 0; processor_ok=false;}
                    if(receiver_ok) { pthread_cancel(instance->receiver_tid); pthread_join(instance->receiver_tid, NULL); instance->receiver_tid = 0; receiver_ok=false;}
                }
            } else {
                perror("Listener: Failed to create processor thread");
                if(receiver_ok) { pthread_cancel(instance->receiver_tid); pthread_join(instance->receiver_tid, NULL); instance->receiver_tid = 0; receiver_ok=false;}
            }
        } else {
             perror("Listener: Failed to create receiver thread");
        }

        if (receiver_ok && processor_ok && timer_ok) {
            instance->is_active = true;
            printf("Listener (SVM %d, Port %u): Instance activated. Worker threads (Recv, Proc, Timer) started.\n", svm_id, port);
            pthread_mutex_unlock(&instance->instance_mutex);

            if (instance->receiver_tid != 0) pthread_join(instance->receiver_tid, NULL);
            printf("Listener (SVM %d, Port %u): Receiver thread joined.\n", svm_id, port);
            if (instance->processor_tid != 0) pthread_join(instance->processor_tid, NULL);
            printf("Listener (SVM %d, Port %u): Processor thread joined.\n", svm_id, port);
            
            // Остановка персонального таймера
            pthread_mutex_lock(&instance->instance_mutex);
            if (instance->timer_tid != 0) { // Проверяем, был ли таймер успешно запущен
                instance->personal_timer_keep_running = false;
                 printf("Listener (SVM %d, Port %u): Signaled instance timer thread to stop.\n", svm_id, port);
            }
            pthread_mutex_unlock(&instance->instance_mutex);

            if (instance->timer_tid != 0) {
                pthread_join(instance->timer_tid, NULL);
                printf("Listener (SVM %d, Port %u): Instance timer thread joined.\n", svm_id, port);
            }

             printf("Listener (SVM %d, Port %u): Worker threads finished. Cleaning up instance...\n", svm_id, port);
             pthread_mutex_lock(&instance->instance_mutex);
             if (instance->client_handle >= 0) {
                 if (instance->io_handle) instance->io_handle->disconnect(instance->io_handle, instance->client_handle);
                 else { close(instance->client_handle); instance->client_handle = -1; }
             }
             if (instance->incoming_queue) { qmq_destroy(instance->incoming_queue); instance->incoming_queue = NULL; }
             instance->is_active = false;
             instance->receiver_tid = 0; instance->processor_tid = 0; instance->timer_tid = 0;
             instance->io_handle = NULL;
             pthread_mutex_unlock(&instance->instance_mutex);
             printf("Listener (SVM %d, Port %u): Instance deactivated. Ready for new connection.\n", svm_id, port);
        } else {
            pthread_mutex_unlock(&instance->instance_mutex);
            fprintf(stderr, "Listener (SVM %d, Port %u): Failed to start all worker threads. Rejecting.\n", svm_id, port);
            if(instance->incoming_queue) { qmq_destroy(instance->incoming_queue); instance->incoming_queue = NULL; }
            if(instance->client_handle >=0) { close(instance->client_handle); instance->client_handle = -1;}
            instance->io_handle = NULL; // Указатель на listener_io не должен быть NULL здесь, т.к. он общий для листенера
        }
    } // end while(keep_running)

    printf("Listener thread for SVM ID %d (Port %u) finished.\n", svm_id, port);
    if (lfd >= 0) { close(lfd); listen_sockets[svm_id] = -1; }
    if(listener_io) listener_io->destroy(listener_io); // Уничтожаем IO интерфейс листенера
    return NULL;
}

// --- Основная функция ---
int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
    // pthread_t timer_tid = 0; // Общий таймер УДАЛЕН
    pthread_t sender_tid = 0;
    // bool common_threads_started = false; // Флаг не совсем актуален в прежнем виде
    int num_svms_to_run = 0;

    printf("SVM Multi-Port Server starting...\n");

    // if (pthread_mutex_init(&svm_instances_mutex, NULL) != 0) { exit(EXIT_FAILURE); } // Глобальный мьютекс для массива пока не нужен
    if (init_svm_app_wide_resources() != 0) { exit(EXIT_FAILURE); } // Переименованная функция
    init_message_handlers();

    printf("SVM: Loading configuration...\n");
    if (load_config("config.ini", &config) != 0) {
        destroy_svm_app_wide_resources(); // Переименованная функция
        // pthread_mutex_destroy(&svm_instances_mutex);
        exit(EXIT_FAILURE);
    }
    num_svms_to_run = config.num_svm_configs_found;
    if (num_svms_to_run == 0) { 
        fprintf(stderr, "SVM: No SVM configurations found. Exiting.\n");
        destroy_svm_app_wide_resources();
        exit(EXIT_FAILURE);
     }
    printf("SVM: Will attempt to start %d instances based on config.\n", num_svms_to_run);

    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        initialize_svm_instance(&svm_instances[i], i,
                                config.svm_settings[i].lak,
                                &config.svm_settings[i]);
        if (pthread_mutex_init(&svm_instances[i].instance_mutex, NULL) != 0) {
            perror("Failed to initialize instance mutex");
            for (int j = 0; j < i; ++j) pthread_mutex_destroy(&svm_instances[j].instance_mutex);
            destroy_svm_app_wide_resources();
            // if (svm_outgoing_queue) qmq_destroy(svm_outgoing_queue); // Очередь еще не создана
            exit(EXIT_FAILURE);
        }
        listen_sockets[i] = -1;
        listener_threads[i] = 0;
        printf("DEBUG SVM MAIN - Instance %d Settings: LAK=0x%02X, simulate_control_failure=%d, "
               "disconnect_after=%d, simulate_timeout=%d, send_warning=%d, tks=%u\n",
               i, svm_instances[i].assigned_lak, svm_instances[i].simulate_control_failure,
               svm_instances[i].disconnect_after_messages, svm_instances[i].simulate_response_timeout,
               svm_instances[i].send_warning_on_confirm, svm_instances[i].warning_tks);
    }

    svm_outgoing_queue = qmq_create(100 * num_svms_to_run); // Размер очереди на основе реально запускаемых
    if (!svm_outgoing_queue) { 
        fprintf(stderr, "SVM: Failed to create outgoing queue.\n");
        goto cleanup_instance_mutexes; 
    }

    signal(SIGINT, handle_shutdown_signal);
    signal(SIGTERM, handle_shutdown_signal);

    int listeners_started = 0;
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        if (config.svm_config_loaded[i]) {
            ListenerArgs *args = malloc(sizeof(ListenerArgs));
            if (!args) { perror("SVM: Failed to allocate listener args"); continue; }
            args->svm_id = i;
            args->port = config.svm_ethernet[i].port;
            args->lak = config.svm_settings[i].lak; // LAK все еще нужен для лога в listener_thread_func

            if (pthread_create(&listener_threads[i], NULL, listener_thread_func, args) != 0) {
                perror("SVM: Failed to create listener thread");
                free(args);
            } else {
                listeners_started++;
            }
        }
    }

    if (listeners_started == 0) {
        fprintf(stderr, "SVM: Failed to start any listeners. Exiting.\n");
        goto cleanup_outgoing_queue;
    }

    printf("SVM: Starting common Sender thread...\n");
    // Общий таймер больше не запускаем здесь
    if (pthread_create(&sender_tid, NULL, sender_thread_func, NULL) != 0) {
        perror("SVM: Failed to create sender thread");
        goto cleanup_listeners_on_error;
    }
    printf("SVM: Sender thread started. %d listeners active. Running...\n", listeners_started);

    printf("SVM Main: Waiting for shutdown signal...\n");
    while(keep_running) {
        sleep(1);
    }
    printf("SVM Main: Shutdown initiated. Waiting for threads to join...\n");

cleanup_listeners_on_error: // Метка для случая, если sender не стартанул
    // Если sender не стартанул, а listener'ы да, нужно их как-то остановить.
    // keep_running уже false из handle_shutdown_signal или будет установлен здесь, если это ошибка старта.
    // handle_shutdown_signal закроет listen_sockets, что должно помочь listener'ам завершиться.

    printf("SVM Main: Waiting for listener threads to join...\n");
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        if (listener_threads[i] != 0) {
            pthread_join(listener_threads[i], NULL);
            printf("SVM Main: Listener thread for SVM ID %d joined.\n", i);
        }
    }
    printf("SVM Main: All listener threads joined.\n");

    if (sender_tid != 0) {
        if (svm_outgoing_queue && !svm_outgoing_queue->shutdown) {
             qmq_shutdown(svm_outgoing_queue);
         }
        pthread_join(sender_tid, NULL);
        printf("SVM Main: Sender thread joined.\n");
    }

cleanup_outgoing_queue:
    if (svm_outgoing_queue) qmq_destroy(svm_outgoing_queue);

cleanup_instance_mutexes:
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        pthread_mutex_destroy(&svm_instances[i].instance_mutex);
    }
    // pthread_mutex_destroy(&svm_instances_mutex); // Глобальный мьютекс для массива не используется активно
    destroy_svm_app_wide_resources(); // Переименованная функция

    printf("SVM: Cleanup finished. Exiting.\n");
	return 0;
}