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
// Включаем оба типа очередей
#include "../utils/ts_queue.h"          // Для Message (если понадобится где-то еще)
#include "../utils/ts_queued_msg_queue.h" // Для QueuedMessage
#include "svm_handlers.h"
#include "svm_timers.h" // Для init/destroy_svm_timer_sync, stop_timer_thread_signal
#include "svm_types.h"  // Для SvmInstance, MAX_SVM_INSTANCES, QueuedMessage

// --- Глобальные переменные ---
SvmInstance svm_instances[MAX_SVM_INSTANCES];         // Массив экземпляров СВ-М
ThreadSafeQueuedMsgQueue *svm_outgoing_queue = NULL;  // Общая исходящая очередь QueuedMessage
IOInterface *io_svm = NULL;                           // Общий IO интерфейс
pthread_mutex_t svm_instances_mutex;                  // Мьютекс для доступа к массиву svm_instances
int listen_socket_fd = -1;                            // Слушающий сокет (если TCP)
volatile bool keep_running = true;                    // Флаг для грациозного завершения

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
    // Игнорируем результат write в обработчике
    ssize_t written __attribute__((unused)) = write(STDOUT_FILENO, msg_ptr, strlen(msg_ptr));

    keep_running = false; // Устанавливаем флаг

    // Аккуратно закрываем слушающий сокет, чтобы accept() разблокировался
    if (listen_socket_fd >= 0) {
        // Используем временную переменную для дескриптора, чтобы избежать гонки с main
        int fd_to_close = listen_socket_fd;
        listen_socket_fd = -1; // Сбрасываем глобальный дескриптор
        if (fd_to_close >= 0) {
           shutdown(fd_to_close, SHUT_RDWR);
           close(fd_to_close);
        }
    }
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
         exit(EXIT_FAILURE); // Фатальная ошибка
    }
}

// --- Основная функция ---
int main() {
    AppConfig config;
    pthread_t timer_tid = 0, sender_tid = 0;
    int active_connections = 0;
    bool common_threads_started = false; // Флаг для корректной очистки

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
    // Используем параметры из конфига
    int num_instances_to_run = config.num_svm_instances; // Используем значение из конфига
    int base_lak = config.base_svm_lak;
    int base_port = config.ethernet.port; // Пока используем основной порт

    if (num_instances_to_run > MAX_SVM_INSTANCES) {
        fprintf(stderr, "SVM: Warning: num_svm_instances (%d) in config exceeds MAX_SVM_INSTANCES (%d). Clamping.\n",
                num_instances_to_run, MAX_SVM_INSTANCES);
        num_instances_to_run = MAX_SVM_INSTANCES;
    }


    // Создание интерфейса IO
    printf("SVM: Creating IO interface type '%s'...\n", config.interface_type);
     if (strcasecmp(config.interface_type, "ethernet") == 0) {
        io_svm = create_ethernet_interface(&config.ethernet);
    } else if (strcasecmp(config.interface_type, "serial") == 0) {
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
    svm_outgoing_queue = qmq_create(100 * num_instances_to_run); // Размер зависит от конфига
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
        goto cleanup_init_error; // Переход к общей секции ошибки инициализации
    }
    common_threads_started = true;

	// --- Сетевая часть (только Ethernet пока) ---
    if (io_svm->type == IO_TYPE_ETHERNET) {
        printf("SVM: Starting Ethernet listener on port %d...\n", base_port);
        listen_socket_fd = io_svm->listen(io_svm);
	    if (listen_socket_fd < 0) {
            fprintf(stderr, "SVM: Failed to start Ethernet listener.\n");
            goto cleanup_init_error; // Переход к общей секции ошибки инициализации
	    }
	    printf("SVM listening (handle: %d). Waiting for up to %d UVM connections...\n", listen_socket_fd, num_instances_to_run);

        // --- Цикл приема соединений ---
        while(keep_running) {
            char client_ip_str[INET_ADDRSTRLEN];
            uint16_t client_port_num;
            int current_listen_fd = listen_socket_fd; // Копируем перед блокирующим вызовом

            if (current_listen_fd < 0) { // Проверяем, не закрыл ли сокет обработчик сигнала
                 printf("SVM: Listener socket closed, exiting accept loop.\n");
                 break;
            }

            // accept теперь часть интерфейса
            int client_handle = io_svm->accept(io_svm, client_ip_str, sizeof(client_ip_str), &client_port_num);

            if (client_handle < 0) {
                // Проверяем errno после accept
                if (!keep_running || errno == EBADF) { // Прервано сигналом или сокет закрыт
                    printf("SVM: Listener interrupted or socket closed.\n");
                } else if (errno == EINTR) {
                    printf("SVM: accept() interrupted, retrying...\n");
                    continue; // Повторяем попытку, если не сигнал завершения
                }
                else { // Другая ошибка accept
                    if (keep_running) perror("SVM: Error accepting connection");
                }
                break; // Выходим из цикла приема соединений при ошибке или завершении
            }

            // Соединение принято, ищем свободный слот
            int instance_slot = -1;
            pthread_mutex_lock(&svm_instances_mutex);
            for (int i = 0; i < num_instances_to_run; ++i) { // Ищем только в пределах num_instances_to_run
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
                instance->io_handle = io_svm;
                instance->assigned_lak = (LogicalAddress)(base_lak + instance_slot);
                instance->current_state = STATE_NOT_INITIALIZED;
                instance->message_counter = 0;
                // Сброс счетчиков
                pthread_mutex_lock(&instance->instance_mutex); // Блокируем экземпляр для сброса
                instance->bcb_counter = 0;
                instance->link_up_changes_counter = 0;
                instance->link_up_low_time_us100 = 0;
                instance->sign_det_changes_counter = 0;
                instance->link_status_timer_counter = 0;
                pthread_mutex_unlock(&instance->instance_mutex);

                // Создаем входящую очередь для экземпляра
                instance->incoming_queue = qmq_create(100);
                if (!instance->incoming_queue) {
                    fprintf(stderr, "SVM: Failed to create incoming queue for instance %d. Rejecting connection.\n", instance_slot);
                    io_svm->disconnect(io_svm, client_handle);
                    pthread_mutex_unlock(&svm_instances_mutex);
                    continue;
                }

                // Запускаем потоки для экземпляра
                if (pthread_create(&instance->receiver_tid, NULL, receiver_thread_func, instance) != 0) {
                    perror("SVM: Failed to create receiver thread");
                    qmq_destroy(instance->incoming_queue);
                    instance->incoming_queue = NULL;
                    io_svm->disconnect(io_svm, client_handle);
                    pthread_mutex_unlock(&svm_instances_mutex);
                    continue;
                }
                if (pthread_create(&instance->processor_tid, NULL, processor_thread_func, instance) != 0) {
                    perror("SVM: Failed to create processor thread");
                    // Пытаемся отменить receiver (не лучший способ, но лучше, чем ничего)
                    pthread_cancel(instance->receiver_tid); // Может не сработать или вызвать утечки
                    // pthread_join(instance->receiver_tid, NULL); // Ждать отмененный поток может быть опасно
                    qmq_destroy(instance->incoming_queue);
                    instance->incoming_queue = NULL;
                    io_svm->disconnect(io_svm, client_handle);
                    pthread_mutex_unlock(&svm_instances_mutex);
                    continue;
                }

                instance->is_active = true; // Помечаем как активный
                active_connections++;
                printf("SVM: Instance %d (LAK 0x%02X) is now active. Total active: %d\n",
                       instance_slot, instance->assigned_lak, active_connections);

            } else {
                printf("SVM: Maximum number of instances (%d) reached or no free slots. Rejecting connection from %s:%u.\n",
                       num_instances_to_run, client_ip_str, client_port_num);
                io_svm->disconnect(io_svm, client_handle);
            }
            pthread_mutex_unlock(&svm_instances_mutex);

        } // end while(keep_running) for accept

    } else { // IO_TYPE_SERIAL
        fprintf(stderr, "SVM: Serial IO type is not supported in this version.\n");
        goto cleanup_init_error; // Ошибка инициализации
    }

    // Нормальный выход из цикла или ошибка accept после старта
    goto main_shutdown_sequence;

// Метка для перехода при ошибках инициализации ДО цикла accept
cleanup_init_error:
    fprintf(stderr, "SVM: Initiating shutdown due to initialization error...\n");
    goto shutdown_common_threads;

// Метка для перехода после нормального завершения или сигнала
main_shutdown_sequence:
    // Логика проверки причины завершения цикла
    if (errno != 0 && errno != EINTR && keep_running) {
         perror("SVM: Listener accept loop exited with error");
    } else if (!keep_running) {
         printf("SVM: Listener loop exited due to shutdown signal or closed socket.\n");
    } else {
         printf("SVM: Listener loop finished normally (unexpected for server).\n");
    }
    printf("SVM: Main loop finished. Cleaning up...\n");
    goto shutdown_common_threads; // Переходим к общей секции

// Общая последовательность остановки потоков
shutdown_common_threads:
    printf("SVM: Shutting down common threads...\n");
    keep_running = false; // Убедимся, что флаг установлен
    stop_timer_thread_signal(); // Сигналим таймеру
    if (svm_outgoing_queue) qmq_shutdown(svm_outgoing_queue); // Закрываем общую исходящую

    // Дожидаемся общие потоки, только если они были успешно запущены
    if (common_threads_started) {
       if (timer_tid != 0) pthread_join(timer_tid, NULL);
       printf("SVM Main: Timer thread joined.\n");
       if (sender_tid != 0) pthread_join(sender_tid, NULL);
       printf("SVM Main: Sender thread joined.\n");
    } else {
       printf("SVM Main: Common threads were not fully started.\n");
    }

    goto cleanup_instance_threads; // Переходим к завершению потоков экземпляров

// Завершение потоков экземпляров
cleanup_instance_threads:
    printf("SVM Main: Closing client connections and instance queues...\n");
    pthread_mutex_lock(&svm_instances_mutex);
    for (int i = 0; i < num_instances_to_run; ++i) {
        if (svm_instances[i].is_active) { // Закрываем только активные
             if (svm_instances[i].client_handle >= 0) {
                  // Сначала пытаемся разбудить потоки
                  shutdown(svm_instances[i].client_handle, SHUT_RDWR);
             }
             if (svm_instances[i].incoming_queue) {
                  qmq_shutdown(svm_instances[i].incoming_queue); // Разбудить processor
             }
        }
    }
    pthread_mutex_unlock(&svm_instances_mutex);

    // Дожидаемся завершения потоков экземпляров
    printf("SVM Main: Joining instance threads...\n");
    active_connections = 0;
    for (int i = 0; i < num_instances_to_run; ++i) {
         bool was_active = false; // Флаг, чтобы закрыть хэндл только один раз
         // Ждем потоки, только если они были созданы
         if (svm_instances[i].receiver_tid != 0) {
             pthread_join(svm_instances[i].receiver_tid, NULL);
             printf("SVM Main: Receiver thread for instance %d joined.\n", i);
             svm_instances[i].receiver_tid = 0;
             was_active = true;
         }
         if (svm_instances[i].processor_tid != 0) {
             pthread_join(svm_instances[i].processor_tid, NULL);
             printf("SVM Main: Processor thread for instance %d joined.\n", i);
             svm_instances[i].processor_tid = 0;
             was_active = true;
         }

         // Закрываем хэндл клиента после завершения потоков
         if (was_active) {
            pthread_mutex_lock(&svm_instances_mutex);
            if (svm_instances[i].client_handle >= 0) {
                if(io_svm) io_svm->disconnect(io_svm, svm_instances[i].client_handle);
                else close(svm_instances[i].client_handle); // На всякий случай
                svm_instances[i].client_handle = -1;
                 printf("SVM Main: Client handle for instance %d closed.\n", i);
            }
            // Помечаем как неактивный окончательно
            svm_instances[i].is_active = false;
            pthread_mutex_unlock(&svm_instances_mutex);
         }
    }
    printf("SVM Main: All instance threads joined.\n");


// --- Очистка общих ресурсов ---
cleanup_queues:
	printf("SVM: Cleaning up queues...\n");
    if (svm_outgoing_queue) qmq_destroy(svm_outgoing_queue);
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) { // Проверяем все слоты
        if (svm_instances[i].incoming_queue) {
            qmq_destroy(svm_instances[i].incoming_queue);
        }
    }

cleanup_io:
    printf("SVM: Cleaning up IO interface...\n");
    // Слушающий сокет уже должен быть закрыт в обработчике сигнала или при ошибке listen
    if (listen_socket_fd >= 0 && io_svm) { // Доп. проверка
         io_svm->disconnect(io_svm, listen_socket_fd);
         printf("SVM: Listener handle closed (in cleanup).\n");
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