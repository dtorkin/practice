/*
 * svm/svm_main.c
 * Описание: Основной файл SVM: инициализация, управление МНОЖЕСТВОМ экземпляров СВ-М,
 * создание потоков (ОБЩИЕ Timer/Sender, ПЕРСОНАЛЬНЫЕ Listener/Receiver/Processor),
 * управление их жизненным циклом. Использует подход "1 поток accept на порт".
 * Интегрирован с Qt для отображения GUI.
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

// Обертка для включения заголовочных файлов Qt
#ifdef __cplusplus
extern "C" {
#endif

// --- Заголовки проекта ---
#include "../config/config.h" // Сначала config.h для MAX_SVM_INSTANCES
#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../io/io_common.h"
#include "../io/io_interface.h"
#include "../utils/ts_queued_msg_queue.h"
#include "svm_handlers.h"
#include "svm_timers.h"
#include "svm_types.h" // Включаем ПОСЛЕ config.h

#ifdef __cplusplus
} // extern "C"
// Включаем заголовки Qt ТОЛЬКО если компилируется как C++
#include <QApplication>
#include "svm_gui.h" // Включаем заголовок нашего окна
#endif


// --- Глобальные переменные ---
AppConfig config;                            // Глобальная конфигурация
SvmInstance svm_instances[MAX_SVM_INSTANCES];  // Массив экземпляров СВ-М
ThreadSafeQueuedMsgQueue *svm_outgoing_queue = NULL; // Общая исходящая очередь
pthread_mutex_t svm_instances_mutex;       // Мьютекс для защиты массива (редко нужен)
int listen_sockets[MAX_SVM_INSTANCES];       // Массив слушающих сокетов
pthread_t listener_threads[MAX_SVM_INSTANCES]; // Массив потоков-слушателей

volatile bool keep_running = true;         // Глобальный флаг работы

// --- Прототипы потоков ---
extern void* receiver_thread_func(void* arg);
extern void* processor_thread_func(void* arg);
extern void* sender_thread_func(void* arg);
extern void* timer_thread_func(void* arg);
void* listener_thread_func(void* arg);

// --- Обработчик сигналов ---
void handle_shutdown_signal(int sig) {
    (void)sig;
    const char msg[] = "\nSVM: Received shutdown signal. Shutting down all listeners and instances...\n";
    ssize_t written __attribute__((unused)) = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    keep_running = false; // Устанавливаем флаг остановки

    // Закрываем ВСЕ слушающие сокеты, чтобы разбудить потоки listener_thread_func
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        int fd = listen_sockets[i];
        if (fd >= 0) {
            listen_sockets[i] = -1;
            shutdown(fd, SHUT_RDWR);
            close(fd);
        }
    }
    // Сигналим очередям и таймеру
    if (svm_outgoing_queue) qmq_shutdown(svm_outgoing_queue);
    stop_timer_thread_signal();
    // Разбудить Processor'ы через закрытие их входящих очередей
    // Это лучше делать в основном потоке при штатном завершении
    // или здесь, если это единственный способ их остановить
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
         pthread_mutex_lock(&svm_instances[i].instance_mutex);
         if (svm_instances[i].is_active && svm_instances[i].incoming_queue) {
              qmq_shutdown(svm_instances[i].incoming_queue);
         }
         pthread_mutex_unlock(&svm_instances[i].instance_mutex);
    }

}

// --- Функция инициализации экземпляра ---
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
    instance->messages_sent_count = 0;

    // Сброс счетчиков таймера
    instance->bcb_counter = 0;
    instance->link_up_changes_counter = 0;
    instance->link_up_low_time_us100 = 0;
    instance->sign_det_changes_counter = 0;
    instance->link_status_timer_counter = 0;

    // Копируем параметры имитации сбоев из глобальной config
    if (id >= 0 && id < MAX_SVM_INSTANCES) {
         instance->assigned_lak = config.svm_settings[id].lak;
         instance->simulate_control_failure = config.svm_settings[id].simulate_control_failure;
         instance->disconnect_after_messages = config.svm_settings[id].disconnect_after_messages;
         instance->simulate_response_timeout = config.svm_settings[id].simulate_response_timeout;
         instance->send_warning_on_confirm = config.svm_settings[id].send_warning_on_confirm;
         instance->warning_tks = config.svm_settings[id].warning_tks;
    } else {
         instance->assigned_lak = 0;
         instance->simulate_control_failure = false;
         instance->disconnect_after_messages = -1;
         instance->simulate_response_timeout = false;
         instance->send_warning_on_confirm = false;
         instance->warning_tks = 0;
    }
    // Мьютекс инициализируется в main
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
    instance->assigned_lak = lak;

    // Создаем IO интерфейс
    EthernetConfig listen_config = {0};
    listen_config.port = port;
    listener_io = create_ethernet_interface(&listen_config);
    if (!listener_io) {
        fprintf(stderr, "Listener (SVM %d): Failed to create IO interface for port %d.\n", svm_id, port);
        return NULL;
    }

    // Начинаем слушать
    lfd = listener_io->listen(listener_io);
    if (lfd < 0) {
        fprintf(stderr, "Listener (SVM %d): Failed to listen on port %d.\n", svm_id, port);
        listener_io->destroy(listener_io);
        return NULL;
    }
    listen_sockets[svm_id] = lfd; // Сохраняем для обработчика сигнала

    printf("Listener thread started for SVM ID %d (LAK 0x%02X, Port %d, Listen FD %d)\n", svm_id, lak, port, lfd);

    while (keep_running) {
        char client_ip_str[INET_ADDRSTRLEN];
        uint16_t client_port_num;
        int client_handle = -1;

        printf("Listener (SVM %d): Waiting for connection on port %d...\n", svm_id, port);
        // Используем accept из СОБСТВЕННОГО IO интерфейса
        client_handle = listener_io->accept(listener_io, client_ip_str, sizeof(client_ip_str), &client_port_num);

        if (client_handle < 0) {
            if (!keep_running || errno == EBADF) {
                 printf("Listener (SVM %d): Accept loop interrupted or socket closed.\n", svm_id);
            } else if (errno == EINTR) {
                 printf("Listener (SVM %d): accept() interrupted, retrying...\n", svm_id);
                continue;
            } else {
                 if(keep_running) perror("Listener accept failed");
            }
            break; // Выходим из цикла при ошибке или завершении
        }

        printf("Listener (SVM %d): Accepted connection from %s:%u (Client FD %d)\n",
               svm_id, client_ip_str, client_port_num, client_handle);

        pthread_mutex_lock(&instance->instance_mutex);
        if (instance->is_active) {
             pthread_mutex_unlock(&instance->instance_mutex);
             fprintf(stderr, "Listener (SVM %d): Instance is already active! Rejecting new connection.\n", svm_id);
             close(client_handle);
             continue;
        }

        // Настраиваем экземпляр
        instance->client_handle = client_handle;
        instance->io_handle = listener_io; // Сохраняем IO
        instance->current_state = STATE_NOT_INITIALIZED;
        instance->message_counter = 0;
        instance->messages_sent_count = 0; // Сброс счетчика для disconnect
        instance->bcb_counter = 0;
        instance->link_up_changes_counter = 0;
        instance->link_up_low_time_us100 = 0;
        instance->sign_det_changes_counter = 0;
        instance->link_status_timer_counter = 0;

        instance->incoming_queue = qmq_create(100);
        if (!instance->incoming_queue) {
             pthread_mutex_unlock(&instance->instance_mutex);
             fprintf(stderr, "Listener (SVM %d): Failed to create incoming queue. Rejecting.\n", svm_id);
             close(client_handle);
             instance->io_handle = NULL;
             continue;
        }

        // Запускаем рабочие потоки
        bool receiver_ok = false, processor_ok = false;
        instance->receiver_tid = 0;
        instance->processor_tid = 0;
        if (pthread_create(&instance->receiver_tid, NULL, receiver_thread_func, instance) == 0) {
            receiver_ok = true;
            if (pthread_create(&instance->processor_tid, NULL, processor_thread_func, instance) == 0) {
                processor_ok = true;
            } else {
                perror("Listener: Failed to create processor thread");
                pthread_cancel(instance->receiver_tid);
                pthread_join(instance->receiver_tid, NULL);
                instance->receiver_tid = 0;
                receiver_ok = false;
            }
        } else {
             perror("Listener: Failed to create receiver thread");
        }

        if (receiver_ok && processor_ok) {
            instance->is_active = true;
            printf("Listener (SVM %d): Instance activated. Worker threads started.\n", svm_id);
            pthread_mutex_unlock(&instance->instance_mutex);

            // --- Ожидание завершения рабочих потоков ---
            if (instance->receiver_tid != 0) pthread_join(instance->receiver_tid, NULL);
            printf("Listener (SVM %d): Receiver thread joined.\n", svm_id);
            if (instance->processor_tid != 0) pthread_join(instance->processor_tid, NULL);
            printf("Listener (SVM %d): Processor thread joined.\n", svm_id);

            // --- Очистка после завершения клиента ---
             printf("Listener (SVM %d): Worker threads finished. Cleaning up instance...\n", svm_id);
             pthread_mutex_lock(&instance->instance_mutex);
             if (instance->client_handle >= 0) {
                 if (instance->io_handle) instance->io_handle->disconnect(instance->io_handle, instance->client_handle);
                 else close(instance->client_handle);
                 instance->client_handle = -1;
             }
             if (instance->incoming_queue) {
                 qmq_destroy(instance->incoming_queue);
                 instance->incoming_queue = NULL;
             }
             instance->is_active = false;
             instance->receiver_tid = 0;
             instance->processor_tid = 0;
             instance->io_handle = NULL; // Сбрасываем указатель
             pthread_mutex_unlock(&instance->instance_mutex);
             printf("Listener (SVM %d): Instance deactivated. Ready for new connection.\n", svm_id);

        } else {
            // Не удалось запустить потоки
            pthread_mutex_unlock(&instance->instance_mutex);
            fprintf(stderr, "Listener (SVM %d): Failed to start worker threads. Rejecting.\n", svm_id);
            if(instance->incoming_queue) qmq_destroy(instance->incoming_queue);
            instance->incoming_queue = NULL;
            close(client_handle);
            instance->io_handle = NULL;
            continue;
        }
    } // end while(keep_running)

    printf("Listener thread for SVM ID %d finished.\n", svm_id);
    if (lfd >= 0) {
         close(lfd);
         listen_sockets[svm_id] = -1;
    }
    if(listener_io) listener_io->destroy(listener_io);
    return NULL;
} // end listener_thread_func


// --- Основная функция ---
int main(int argc, char *argv[]) { // <-- Возвращаем argc, argv для QApplication
    pthread_t timer_tid = 0, sender_tid = 0;
    bool common_threads_started = false;
    int num_svms_to_run = 0;
    int result_code = EXIT_SUCCESS; // Код возврата программы

    printf("SVM Multi-Port Server with Qt GUI starting...\n");

    // --- Инициализация НЕ-Qt части ---
    if (pthread_mutex_init(&svm_instances_mutex, NULL) != 0) { perror("SVM: Mutex init failed"); exit(EXIT_FAILURE); }
    if (init_svm_timer_sync() != 0) { perror("SVM: Timer sync init failed"); exit(EXIT_FAILURE); }
    init_message_handlers();

    printf("SVM: Loading configuration...\n");
    if (load_config("config.ini", &config) != 0) { // Загружаем ВСЮ конфигурацию
        result_code = EXIT_FAILURE; goto cleanup_sync;
    }
    num_svms_to_run = config.num_svm_configs_found;
    if (num_svms_to_run == 0) {
        fprintf(stderr, "SVM: No SVM configurations found in config file. Exiting.\n");
        result_code = EXIT_FAILURE; goto cleanup_sync;
    }
    printf("SVM: Will attempt to start %d instances based on config.\n", num_svms_to_run);

    // Инициализация экземпляров и их мьютексов
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        initialize_svm_instance(&svm_instances[i], i); // Использует глобальную config
        if (pthread_mutex_init(&svm_instances[i].instance_mutex, NULL) != 0) {
            perror("Failed to initialize instance mutex");
            for (int j = 0; j < i; ++j) pthread_mutex_destroy(&svm_instances[j].instance_mutex);
            pthread_mutex_destroy(&svm_instances_mutex);
            exit(EXIT_FAILURE);
        }
        listen_sockets[i] = -1;
        listener_threads[i] = 0;
    }

    svm_outgoing_queue = qmq_create(100 * num_svms_to_run);
    if (!svm_outgoing_queue) { result_code = EXIT_FAILURE; goto cleanup_sync; }

    // Установка сигналов (Qt может их перехватывать)
    // signal(SIGINT, handle_shutdown_signal);
    // signal(SIGTERM, handle_shutdown_signal);
    printf("SVM: Signal handlers might be overridden by Qt. Close the window to exit.\n");

    // --- Запуск потоков-слушателей SVM ---
    int listeners_started = 0;
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        if (config.svm_config_loaded[i]) { // Запускаем только для сконфигурированных
             ListenerArgs *args = malloc(sizeof(ListenerArgs));
             if (!args) { perror("SVM: Failed to allocate listener args"); continue; }
             args->svm_id = i;
             args->port = config.svm_ethernet[i].port;
             args->lak = config.svm_settings[i].lak;

             if (pthread_create(&listener_threads[i], NULL, listener_thread_func, args) != 0) {
                 perror("SVM: Failed to create listener thread");
                 free(args);
                 // TODO: Остановить уже запущенные?
             } else {
                 listeners_started++;
                  printf("SVM: Listener thread %d initiated for Port %d (LAK 0x%02X).\n", i, args->port, args->lak);
             }
        }
    } // end for start listeners

    if (listeners_started == 0) {
        fprintf(stderr, "SVM: Failed to start any listeners. Exiting.\n");
        result_code = EXIT_FAILURE; goto cleanup_queues;
    }

    // --- Запуск общих потоков SVM ---
    printf("SVM: Starting common threads (Timer, Sender)...\n");
    if (pthread_create(&timer_tid, NULL, timer_thread_func, svm_instances) != 0) {
        perror("SVM: Failed to create timer thread");
        result_code = EXIT_FAILURE; goto cleanup_listeners;
    }
    if (pthread_create(&sender_tid, NULL, sender_thread_func, NULL) != 0) {
        perror("SVM: Failed to create sender thread");
        result_code = EXIT_FAILURE; goto cleanup_timer;
    }
    common_threads_started = true;
    printf("SVM: All background threads started. Starting GUI...\n");

    // --- Инициализация и запуск Qt ---
#ifdef __cplusplus // Код компилируется только если используется C++ линкер
    int qt_exit_code = 0;
    try {
        QApplication app(argc, argv);
        SvmMainWindow mainWindow;
		printf("SVM Main: Showing main window...\n"); 
        mainWindow.show();
		printf("SVM Main: Entering Qt event loop...\n"); 
        qt_exit_code = app.exec(); // Запускаем цикл обработки событий Qt
        printf("SVM: Qt application finished with code %d.\n", qt_exit_code);
    } catch (const std::exception& e) {
        fprintf(stderr, "SVM: Caught C++ exception: %s\n", e.what());
        result_code = EXIT_FAILURE;
    } catch (...) {
        fprintf(stderr, "SVM: Caught unknown C++ exception.\n");
        result_code = EXIT_FAILURE;
    }
    // --- Завершение потоков SVM ПОСЛЕ закрытия окна ---
    // Пользователь закрыл окно, теперь останавливаем все остальное
    if (keep_running) { // Если сигнал не пришел раньше
        printf("SVM Main: GUI closed. Initiating shutdown of background threads...\n");
        keep_running = false; // Устанавливаем флаг
        // Закрываем слушающие сокеты и сигналим очередям/таймеру
        for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
            int fd = listen_sockets[i];
            if (fd >= 0) { listen_sockets[i] = -1; shutdown(fd, SHUT_RDWR); close(fd); }
        }
        if (svm_outgoing_queue) qmq_shutdown(svm_outgoing_queue);
        stop_timer_thread_signal();
         // Разбудить Processor'ы
        for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
             pthread_mutex_lock(&svm_instances[i].instance_mutex);
             if (svm_instances[i].is_active && svm_instances[i].incoming_queue) {
                  qmq_shutdown(svm_instances[i].incoming_queue);
             }
             pthread_mutex_unlock(&svm_instances[i].instance_mutex);
        }
    }
    goto cleanup_timer; // Переходим к ожиданию потоков

#else // Заглушка, если компилируется как чистый C
    printf("SVM: Qt support not compiled. Running in console mode.\n");
    printf("SVM: Press Ctrl+C to exit.\n");
    signal(SIGINT, handle_shutdown_signal); // Восстанавливаем обработчик
    signal(SIGTERM, handle_shutdown_signal);
    while(keep_running) {
        sleep(1);
    }
    printf("SVM Main: Shutdown initiated. Waiting for threads to join...\n");
    goto cleanup_timer; // Переходим к очистке
#endif

// --- Очистка ---
cleanup_timer:
    if (common_threads_started && timer_tid != 0) {
        // stop_timer_thread_signal(); // Уже вызван выше или в обработчике сигнала
        pthread_join(timer_tid, NULL);
        printf("SVM Main: Timer thread joined.\n");
    }
cleanup_listeners:
    printf("SVM Main: Waiting for listener threads to join...\n");
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        if (listener_threads[i] != 0) {
            pthread_join(listener_threads[i], NULL);
             printf("SVM Main: Listener thread for SVM ID %d joined.\n", i);
        }
    }
    printf("SVM Main: All listener threads joined.\n");
    if (common_threads_started && sender_tid != 0) {
        if (svm_outgoing_queue && !svm_outgoing_queue->shutdown) { qmq_shutdown(svm_outgoing_queue); }
        pthread_join(sender_tid, NULL);
        printf("SVM Main: Sender thread joined.\n");
    }

cleanup_queues:
	printf("SVM: Cleaning up queues...\n");
    if (svm_outgoing_queue) qmq_destroy(svm_outgoing_queue);
    // Входящие очереди уничтожаются внутри listener_thread_func

cleanup_sync:
    printf("SVM: Cleaning up synchronization primitives...\n");
    destroy_svm_timer_sync();
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        pthread_mutex_destroy(&svm_instances[i].instance_mutex);
    }
    pthread_mutex_destroy(&svm_instances_mutex);

    printf("SVM: Cleanup finished. Exiting with code %d.\n", result_code);
	return result_code;
} // end main