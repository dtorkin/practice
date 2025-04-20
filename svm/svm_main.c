/*
 * svm/svm_main.c
 *
 * Описание:
 * Основной файл SVM: инициализация, управление МНОЖЕСТВОМ экземпляров СВ-М,
 * создание потоков (приемники, обработчики, общий отправитель, общий таймер),
 * управление их жизненным циклом.
 */

// --- Сначала системные заголовки ---
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // Для strcasecmp
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>

// --- Затем заголовки нашего проекта ---
#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../io/io_common.h"
#include "../io/io_interface.h"
#include "../config/config.h"
#include "../utils/ts_queue.h"
#include "../utils/ts_queued_msg_queue.h"
#include "svm_handlers.h"
#include "svm_timers.h" // Для init/destroy_svm_timer_sync, stop_timer_thread_signal
#include "svm_types.h"  // Для SvmInstance, MAX_SVM_INSTANCES, QueuedMessage

// --- Глобальные переменные ---
SvmInstance svm_instances[MAX_SVM_INSTANCES]; // Массив экземпляров СВ-М
ThreadSafeQueuedMsgQueue *svm_outgoing_queue = NULL;   // Общая исходящая очередь
IOInterface *io_svm = NULL;                   // Общий IO интерфейс
ThreadSafeQueuedMsgQueue *svm_outgoing_queue = NULL;   // Общая исходящая очередь QueuedMessage
pthread_mutex_t svm_instances_mutex;          // Мьютекс для доступа к массиву svm_instances
int listen_socket_fd = -1;                    // Слушающий сокет (если TCP)
volatile bool keep_running = true;            // Флаг для грациозного завершения (заменяет global_timer_keep_running)

// --- Прототипы функций потоков ---
extern void* receiver_thread_func(void* arg);  // Принимает SvmInstance*
extern void* processor_thread_func(void* arg); // Принимает SvmInstance*
extern void* sender_thread_func(void* arg);    // Общий, arg не используется
extern void* timer_thread_func(void* arg);     // Общий, принимает SvmInstance* (массив)

// --- Обработчик сигналов ---
void handle_shutdown_signal(int sig) {
    const char msg_int[] = "\nSVM: Received SIGINT. Shutting down...\n";
    const char msg_term[] = "\nSVM: Received SIGTERM. Shutting down...\n";
    const char msg_unknown[] = "\nSVM: Received unknown signal. Shutting down...\n";
    const char *msg_ptr = msg_unknown;

    switch(sig) {
        case SIGINT: msg_ptr = msg_int; break;
        case SIGTERM: msg_ptr = msg_term; break;
    }
    write(STDOUT_FILENO, msg_ptr, strlen(msg_ptr));

    keep_running = false; // Устанавливаем флаг

    // Аккуратно закрываем слушающий сокет, чтобы accept() разблокировался
    // Не используем io_svm->disconnect здесь, т.к. это обработчик сигнала
    if (listen_socket_fd >= 0) {
        shutdown(listen_socket_fd, SHUT_RDWR);
        close(listen_socket_fd);
        listen_socket_fd = -1;
    }
    // Не вызываем здесь другие НЕ signal-safe функции
}

// --- Функция инициализации экземпляра ---
void initialize_svm_instance(SvmInstance *instance, int id) {
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
    if (pthread_mutex_init(&instance->instance_mutex, NULL) != 0) {
         perror("Failed to initialize instance mutex");
         // Ошибка фатальна, но обработаем в main
         exit(EXIT_FAILURE); // Или другая обработка
    }
}

// --- Основная функция ---
int main() {
    AppConfig config;
    pthread_t timer_tid = 0, sender_tid = 0;
    int active_connections = 0;

    printf("SVM Multi-Instance Emulator starting...\n");

    // --- Инициализация ---
    if (pthread_mutex_init(&svm_instances_mutex, NULL) != 0) {
        perror("SVM: Failed to initialize instances mutex");
        exit(EXIT_FAILURE);
    }
    if (init_svm_timer_sync() != 0) { // Инициализация мьютекса/cond таймера
        pthread_mutex_destroy(&svm_instances_mutex);
        exit(EXIT_FAILURE);
    }
    init_message_handlers(); // Инициализация диспетчера сообщений

    // Инициализация всех экземпляров
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        initialize_svm_instance(&svm_instances[i], i);
    }

    // Загрузка конфигурации
    printf("SVM: Loading configuration...\n");
    if (load_config("config.ini", &config) != 0) {
        goto cleanup_sync;
    }
    // TODO: Использовать config.num_svm_instances, config.base_svm_port, config.base_svm_lak
    int base_lak = LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1; // Используем базовый адрес из протокола
    int base_port = config.ethernet.port; // Используем порт из конфига как базовый

    // Создание интерфейса IO
    printf("SVM: Creating IO interface type '%s'...\n", config.interface_type);
     if (strcasecmp(config.interface_type, "ethernet") == 0) {
        io_svm = create_ethernet_interface(&config.ethernet);
    } else if (strcasecmp(config.interface_type, "serial") == 0) {
        // TODO: Поддержка нескольких COM-портов потребует изменений в IO и конфиге
        fprintf(stderr, "SVM: Serial interface currently not supported for multi-instance.\n");
        goto cleanup_sync;
    } else {
         fprintf(stderr, "SVM: Unsupported interface type '%s'.\n", config.interface_type);
         goto cleanup_sync;
    }
    if (!io_svm) {
         fprintf(stderr, "SVM: Failed to create IOInterface.\n");
         goto cleanup_sync;
    }

    // Создание общей исходящей очереди
svm_outgoing_queue = qmq_create(100 * MAX_SVM_INSTANCES); // Используем новую функцию
    if (!svm_outgoing_queue) {
        fprintf(stderr, "SVM: Failed to create global outgoing queue.\n");
        goto cleanup_io;
    }

    // Установка обработчиков сигналов
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_shutdown_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // --- Запуск общих потоков ---
    printf("SVM: Starting common threads (Timer, Sender)...\n");
    if (pthread_create(&timer_tid, NULL, timer_thread_func, svm_instances) != 0) {
        perror("SVM: Failed to create timer thread");
        goto cleanup_queues;
    }
    if (pthread_create(&sender_tid, NULL, sender_thread_func, NULL) != 0) {
        perror("SVM: Failed to create sender thread");
        // Нужно остановить таймер перед выходом
        keep_running = false;
        stop_timer_thread_signal();
        pthread_join(timer_tid, NULL);
        goto cleanup_queues;
    }

	// --- Сетевая часть (только Ethernet пока) ---
    if (io_svm->type == IO_TYPE_ETHERNET) {
        printf("SVM: Starting Ethernet listener on port %d...\n", base_port);
        listen_socket_fd = io_svm->listen(io_svm);
	    if (listen_socket_fd < 0) {
            fprintf(stderr, "SVM: Failed to start Ethernet listener.\n");
            goto cleanup_threads;
	    }
	    printf("SVM listening (handle: %d). Waiting for UVM connections...\n", listen_socket_fd);

        // --- Цикл приема соединений ---
        while(keep_running) {
            char client_ip_str[INET_ADDRSTRLEN];
            uint16_t client_port_num;
            // accept теперь часть интерфейса
            int client_handle = io_svm->accept(io_svm, client_ip_str, sizeof(client_ip_str), &client_port_num);

            if (client_handle < 0) {
                if (errno == EINTR && !keep_running) { // Прервано сигналом завершения
                    printf("SVM: Listener interrupted by shutdown signal.\n");
                } else if (keep_running) { // Другая ошибка accept
                    perror("SVM: Error accepting connection");
                }
                break; // Выходим из цикла приема соединений
            }

            // Соединение принято, ищем свободный слот
            int instance_slot = -1;
            pthread_mutex_lock(&svm_instances_mutex);
            for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
                if (!svm_instances[i].is_active) {
                    instance_slot = i;
                    break;
                }
            }

            if (instance_slot != -1) {
                SvmInstance *instance = &svm_instances[instance_slot];
                printf("SVM: Accepted connection from %s:%u, assigning to instance %d.\n",
                       client_ip_str, client_port_num, instance_slot);

                // Настраиваем экземпляр
                instance->client_handle = client_handle;
                instance->io_handle = io_svm; // Указатель на общий интерфейс
                instance->assigned_lak = (LogicalAddress)(base_lak + instance_slot); // Назначаем LAK
                instance->current_state = STATE_NOT_INITIALIZED; // Сброс состояния
                instance->message_counter = 0; // Сброс счетчика
                // Сброс счетчиков таймера (BCB и линии)
                instance->bcb_counter = 0;
                instance->link_up_changes_counter = 0;
                instance->link_up_low_time_us100 = 0;
                instance->sign_det_changes_counter = 0;
                instance->link_status_timer_counter = 0;

                // Создаем входящую очередь для экземпляра
                instance->incoming_queue = qmq_create(100);
                if (!instance->incoming_queue) {
                    fprintf(stderr, "SVM: Failed to create incoming queue for instance %d. Rejecting connection.\n", instance_slot);
                    io_svm->disconnect(io_svm, client_handle);
                    pthread_mutex_unlock(&svm_instances_mutex);
                    continue; // Ждем следующее соединение
                }

                // Запускаем потоки для экземпляра
                if (pthread_create(&instance->receiver_tid, NULL, receiver_thread_func, instance) != 0) {
                    perror("SVM: Failed to create receiver thread");
                    queue_destroy(instance->incoming_queue);
                    instance->incoming_queue = NULL;
                    io_svm->disconnect(io_svm, client_handle);
                    pthread_mutex_unlock(&svm_instances_mutex);
                    continue;
                }
                if (pthread_create(&instance->processor_tid, NULL, processor_thread_func, instance) != 0) {
                    perror("SVM: Failed to create processor thread");
                    // Нужно остановить receiver
                    pthread_cancel(instance->receiver_tid); // Не лучший способ
                    pthread_join(instance->receiver_tid, NULL);
                    queue_destroy(instance->incoming_queue);
                    instance->incoming_queue = NULL;
                    io_svm->disconnect(io_svm, client_handle);
                    pthread_mutex_unlock(&svm_instances_mutex);
                    continue;
                }

                instance->is_active = true; // Помечаем как активный ПОСЛЕ запуска потоков
                active_connections++;
                printf("SVM: Instance %d (LAK 0x%02X) is now active. Total active: %d\n",
                       instance_slot, instance->assigned_lak, active_connections);

            } else {
                printf("SVM: Maximum number of instances (%d) reached. Rejecting connection from %s:%u.\n",
                       MAX_SVM_INSTANCES, client_ip_str, client_port_num);
                io_svm->disconnect(io_svm, client_handle);
            }
            pthread_mutex_unlock(&svm_instances_mutex);

        } // end while(keep_running) for accept

    } else { // IO_TYPE_SERIAL
        // Логика для Serial не реализована для мульти-инстанс
        fprintf(stderr, "SVM: Serial IO type is not supported in this version.\n");
    }


cleanup_threads: // Сюда попадаем при ошибке запуска общих потоков или листенера
    printf("SVM: Initiating shutdown due to initialization error...\n");
    keep_running = false; // Устанавливаем флаг
    stop_timer_thread_signal(); // Сигналим таймеру
    if (svm_outgoing_queue) qmq_shutdown(svm_outgoing_queue); // Закрываем общую исходящую

    // Дожидаемся общие потоки, если они были запущены
    if (timer_tid != 0) pthread_join(timer_tid, NULL);
    printf("SVM Main: Timer thread joined.\n");
    if (sender_tid != 0) pthread_join(sender_tid, NULL);
    printf("SVM Main: Sender thread joined.\n");
    // Потоки экземпляров не запускались

    goto cleanup_queues; // Переходим к очистке очередей и т.д.

main_shutdown_sequence: // Сюда перейдем после нормального завершения цикла accept
    printf("SVM: Main loop finished or shutdown signaled. Cleaning up...\n");

    // 1. Сигнализируем общим потокам
    keep_running = false; // Убедимся, что флаг установлен
    stop_timer_thread_signal(); // Сигналим таймеру
    if (svm_outgoing_queue) queue_shutdown(svm_outgoing_queue); // Закрываем общую исходящую

    // 2. Сигнализируем потокам экземпляров (закрываем их сокеты и входящие очереди)
    printf("SVM Main: Closing client connections and instance queues...\n");
    pthread_mutex_lock(&svm_instances_mutex);
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        if (svm_instances[i].is_active) {
             if (svm_instances[i].client_handle >= 0) {
                  shutdown(svm_instances[i].client_handle, SHUT_RDWR); // Разбудить receiver
                  // disconnect вызывать не будем, т.к. receiver может быть еще активен
                  // close(svm_instances[i].client_handle); // close сделаем после join
             }
             if (svm_instances[i].incoming_queue) {
                  queue_shutdown(svm_instances[i].incoming_queue); // Разбудить processor
             }
             // Не меняем is_active здесь, пусть receiver сам себя пометит
        }
    }
    pthread_mutex_unlock(&svm_instances_mutex);

    // 3. Дожидаемся завершения ВСЕХ потоков
    printf("SVM Main: Joining threads...\n");
    if (timer_tid != 0) pthread_join(timer_tid, NULL);
    printf("SVM Main: Timer thread joined.\n");
    if (sender_tid != 0) pthread_join(sender_tid, NULL);
    printf("SVM Main: Sender thread joined.\n");

    active_connections = 0; // Сбрасываем счетчик перед join'ом потоков экземпляров
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
         // Не нужно проверять is_active, join'им все созданные потоки
         if (svm_instances[i].receiver_tid != 0) {
             pthread_join(svm_instances[i].receiver_tid, NULL);
             printf("SVM Main: Receiver thread for instance %d joined.\n", i);
             svm_instances[i].receiver_tid = 0; // Сброс ID
         }
         if (svm_instances[i].processor_tid != 0) {
             pthread_join(svm_instances[i].processor_tid, NULL);
             printf("SVM Main: Processor thread for instance %d joined.\n", i);
             svm_instances[i].processor_tid = 0; // Сброс ID
         }
         // Закрываем хэндл клиента теперь, когда потоки завершены
         pthread_mutex_lock(&svm_instances_mutex);
         if (svm_instances[i].client_handle >= 0) {
              if(io_svm) io_svm->disconnect(io_svm, svm_instances[i].client_handle);
              else close(svm_instances[i].client_handle);
              svm_instances[i].client_handle = -1;
              svm_instances[i].is_active = false; // Финальная установка неактивности
              printf("SVM Main: Client handle for instance %d closed.\n", i);
         }
         pthread_mutex_unlock(&svm_instances_mutex);
    }
    printf("SVM Main: All instance threads joined.\n");


    // --- Очистка ресурсов ---
cleanup_queues:
	printf("SVM: Cleaning up queues...\n");
    if (svm_outgoing_queue) qmq_destroy(svm_outgoing_queue);
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        if (svm_instances[i].incoming_queue) {
            qmq_destroy(svm_instances[i].incoming_queue);
        }
    }

cleanup_io:
    printf("SVM: Cleaning up IO interface...\n");
    if (listen_socket_fd >= 0) { // Закрываем слушающий сокет, если он еще открыт
         if (io_svm) io_svm->disconnect(io_svm, listen_socket_fd);
         else close(listen_socket_fd);
         printf("SVM: Listener handle closed.\n");
    }
    if (io_svm) io_svm->destroy(io_svm);

cleanup_sync:
    printf("SVM: Cleaning up synchronization primitives...\n");
    destroy_svm_timer_sync(); // Уничтожаем мьютекс/cond таймера
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        pthread_mutex_destroy(&svm_instances[i].instance_mutex);
    }
    pthread_mutex_destroy(&svm_instances_mutex); // Уничтожаем мьютекс массива

    printf("SVM: Cleanup finished. Exiting.\n");
	return 0;
}