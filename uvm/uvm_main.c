/*
 * uvm/uvm_main.c
 * Описание: Основной файл UVM: инициализация, управление несколькими
 * соединениями с SVM, создание потоков, отправка запросов, обработка ответов.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h> // Для nanosleep
#include <sys/socket.h>

#include "../config/config.h"
#include "../io/io_interface.h"
#include "../protocol/protocol_defs.h"
#include "../protocol/message_builder.h"
#include "../protocol/message_utils.h"
#include "../utils/ts_queue_req.h"     // Очередь запросов к Sender'у
#include "../utils/ts_uvm_resp_queue.h" // Новая очередь ответов от Receiver'ов
#include "uvm_types.h"
#include "uvm_utils.h" // Для uvm_request_type_to_message_name

// --- Глобальные переменные ---
AppConfig config;                     // Загруженная конфигурация
UvmSvmLink svm_links[MAX_SVM_CONFIGS]; // Массив состояний соединений с SVM
pthread_mutex_t uvm_links_mutex;      // Мьютекс для защиты svm_links

ThreadSafeQueueReq *uvm_outgoing_request_queue = NULL; // Очередь к Sender'у
ThreadSafeUvmRespQueue *uvm_incoming_response_queue = NULL; // Очередь от Receiver'ов

volatile bool uvm_keep_running = true;

// Для синхронизации отправки сообщений
volatile int uvm_outstanding_sends = 0;
pthread_cond_t uvm_all_sent_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t uvm_send_counter_mutex = PTHREAD_MUTEX_INITIALIZER;

// Прототипы потоков
void* uvm_sender_thread_func(void* arg);
void* uvm_receiver_thread_func(void* arg); // Принимает UvmSvmLink*

// Обработчик сигналов
void uvm_handle_shutdown_signal(int sig) {
    const char msg[] = "\nUVM: Received shutdown signal. Exiting...\n";
    ssize_t written __attribute__((unused)) = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    uvm_keep_running = false;

    // Сигналим очередям, чтобы разбудить потоки
    if (uvm_outgoing_request_queue) queue_req_shutdown(uvm_outgoing_request_queue);
    if (uvm_incoming_response_queue) uvq_shutdown(uvm_incoming_response_queue);

    // Сигналим условию ожидания отправки
    pthread_mutex_lock(&uvm_send_counter_mutex);
    uvm_outstanding_sends = 0; // Чтобы main не ждал вечно
    pthread_cond_signal(&uvm_all_sent_cond);
    pthread_mutex_unlock(&uvm_send_counter_mutex);
}

// Функция отправки запроса Sender'у
bool send_uvm_request(UvmRequest *request) {
    if (!uvm_outgoing_request_queue || !request) return false;

    // Увеличиваем счетчик перед отправкой
    if (request->type == UVM_REQ_SEND_MESSAGE) {
        pthread_mutex_lock(&uvm_send_counter_mutex);
        uvm_outstanding_sends++;
        //printf("Main: Incremented outstanding sends to %d\n", uvm_outstanding_sends);
        pthread_mutex_unlock(&uvm_send_counter_mutex);
    }

    if (!queue_req_enqueue(uvm_outgoing_request_queue, request)) {
        fprintf(stderr, "UVM Main: Failed to enqueue request (type %d)\n", request->type);
        // Уменьшаем счетчик, если не удалось добавить
        if (request->type == UVM_REQ_SEND_MESSAGE) {
             pthread_mutex_lock(&uvm_send_counter_mutex);
             if(uvm_outstanding_sends > 0) uvm_outstanding_sends--;
             pthread_mutex_unlock(&uvm_send_counter_mutex);
        }
        return false;
    }
    return true;
}

// Функция ожидания отправки всех сообщений
void wait_for_outstanding_sends(void) {
    pthread_mutex_lock(&uvm_send_counter_mutex);
    while (uvm_outstanding_sends > 0 && uvm_keep_running) {
        printf("UVM Main: Waiting for %d outstanding sends...\n", uvm_outstanding_sends);
        // Ждем сигнала от Sender'а
        pthread_cond_wait(&uvm_all_sent_cond, &uvm_send_counter_mutex);
    }
     if (uvm_outstanding_sends == 0) {
         printf("UVM Main: All outstanding messages sent or processed.\n");
     } else {
          printf("UVM Main: Exiting wait loop due to shutdown signal (outstanding: %d).\n", uvm_outstanding_sends);
     }
    pthread_mutex_unlock(&uvm_send_counter_mutex);
}


// Основная функция
int main(int argc, char *argv[]) {
    pthread_t sender_tid = 0;
    int active_svm_count = 0;
    // Режим работы по умолчанию или из аргументов
    RadarMode mode = MODE_DR; // По умолчанию ДР
    if (argc > 1) {
        if (strcasecmp(argv[1], "OP") == 0) mode = MODE_OR;
        else if (strcasecmp(argv[1], "OP1") == 0) mode = MODE_OR1;
        else if (strcasecmp(argv[1], "DR") == 0) mode = MODE_DR;
        else if (strcasecmp(argv[1], "VR") == 0) mode = MODE_VR;
    }

    // Инициализация мьютекса для svm_links
    if (pthread_mutex_init(&uvm_links_mutex, NULL) != 0) {
        perror("UVM: Failed to initialize links mutex");
        exit(EXIT_FAILURE);
    }

    // Инициализация массива svm_links
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        svm_links[i].id = i;
        svm_links[i].io_handle = NULL;
        svm_links[i].connection_handle = -1;
        svm_links[i].status = UVM_LINK_INACTIVE;
        svm_links[i].receiver_tid = 0;
        svm_links[i].assigned_lak = 0; // Будет установлен из конфига
    }

    // Загрузка конфигурации
    printf("UVM: Загрузка конфигурации...\n");
    if (load_config("config.ini", &config) != 0) { exit(EXIT_FAILURE); }
    int num_svms_in_config = config.num_svm_configs_found;
    printf("UVM: Found %d SVM configurations in config file.\n", num_svms_in_config);
    if (num_svms_in_config == 0) {
        fprintf(stderr, "UVM: No SVM configurations found in config.ini. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    // Создание очередей
    uvm_outgoing_request_queue = queue_req_create(50); // Очередь запросов к Sender'у
    uvm_incoming_response_queue = uvq_create(50 * num_svms_in_config); // Очередь ответов от Receiver'ов
    if (!uvm_outgoing_request_queue || !uvm_incoming_response_queue) {
        fprintf(stderr, "UVM: Failed to create message queues.\n");
        goto cleanup_queues;
    }

    // Установка сигналов
    signal(SIGINT, uvm_handle_shutdown_signal);
    signal(SIGTERM, uvm_handle_shutdown_signal);

    // --- Подключение к SVM ---
    printf("UVM: Connecting to SVMs...\n");
    pthread_mutex_lock(&uvm_links_mutex);
    for (int i = 0; i < num_svms_in_config; ++i) {
         if (!config.svm_config_loaded[i]) continue; // Пропускаем, если конфиг не найден

         printf("UVM: Attempting to connect to SVM ID %d (Port: %d)...\n", i, config.svm_ethernet[i].port);
         svm_links[i].status = UVM_LINK_CONNECTING;
         svm_links[i].assigned_lak = config.svm_settings[i].lak; // Сохраняем ожидаемый LAK

         // Создаем отдельный IO интерфейс для каждого соединения
         // Используем данные из config.uvm_ethernet_target для IP и config.svm_ethernet[i].port для порта
         EthernetConfig target_config = config.uvm_ethernet_target; // Копируем IP UVM та겟а
         target_config.port = config.svm_ethernet[i].port; // Устанавливаем нужный порт SVM

         svm_links[i].io_handle = create_ethernet_interface(&target_config);
         if (!svm_links[i].io_handle) {
             fprintf(stderr, "UVM: Failed to create IO interface for SVM ID %d.\n", i);
             svm_links[i].status = UVM_LINK_FAILED;
             continue;
         }

         // Выполняем подключение
         svm_links[i].connection_handle = svm_links[i].io_handle->connect(svm_links[i].io_handle);
         if (svm_links[i].connection_handle < 0) {
             fprintf(stderr, "UVM: Failed to connect to SVM ID %d (Port: %d).\n", i, target_config.port);
             svm_links[i].status = UVM_LINK_FAILED;
             svm_links[i].io_handle->destroy(svm_links[i].io_handle); // Освобождаем интерфейс
             svm_links[i].io_handle = NULL;
         } else {
             printf("UVM: Successfully connected to SVM ID %d (Handle: %d).\n", i, svm_links[i].connection_handle);
             svm_links[i].status = UVM_LINK_ACTIVE;
             active_svm_count++;
         }
    }
    pthread_mutex_unlock(&uvm_links_mutex);

    if (active_svm_count == 0) {
        fprintf(stderr, "UVM: Failed to connect to any SVM. Exiting.\n");
        goto cleanup_queues;
    }
     printf("UVM: Connected to %d out of %d configured SVMs.\n", active_svm_count, num_svms_in_config);

    // --- Запуск потоков ---
    printf("UVM: Запуск потоков Sender и Receiver(s)...\n");
    if (pthread_create(&sender_tid, NULL, uvm_sender_thread_func, NULL) != 0) {
        perror("UVM: Failed to create sender thread");
        goto cleanup_connections;
    }

    // Запускаем Receiver'ы для активных соединений
    pthread_mutex_lock(&uvm_links_mutex);
    for (int i = 0; i < num_svms_in_config; ++i) {
        if (svm_links[i].status == UVM_LINK_ACTIVE) {
            if (pthread_create(&svm_links[i].receiver_tid, NULL, uvm_receiver_thread_func, &svm_links[i]) != 0) {
                perror("UVM: Failed to create receiver thread");
                svm_links[i].status = UVM_LINK_FAILED; // Помечаем как ошибку
                // TODO: Более чистая остановка уже запущенных потоков
                uvm_keep_running = false; // Инициируем общую остановку
            }
        }
    }
    pthread_mutex_unlock(&uvm_links_mutex);

    if (!uvm_keep_running) goto cleanup_threads; // Если не удалось запустить все receiver'ы

    printf("UVM: Потоки запущены. Режим работы: %d\n", mode);


    // --- Основная логика UVM ---
    // Отправка команд всем активным SVM
    UvmRequest request;
    request.type = UVM_REQ_SEND_MESSAGE;

    printf("\n--- Подготовка к сеансу наблюдения ---\n");
    for (int i = 0; i < num_svms_in_config; ++i) {
        pthread_mutex_lock(&uvm_links_mutex);
        bool should_send = (svm_links[i].status == UVM_LINK_ACTIVE);
        pthread_mutex_unlock(&uvm_links_mutex);
        if (should_send) {
            request.target_svm_id = i;
            // 1. Инициализация канала
            request.message = create_init_channel_message(LOGICAL_ADDRESS_UVM, svm_links[i].assigned_lak, 0);
            send_uvm_request(&request);
            // 2. Провести контроль
            request.message = create_provesti_kontrol_message(0x01, 1); // ТК=1
            send_uvm_request(&request);
            // 3. Выдать результаты контроля
            request.message = create_vydat_rezultaty_kontrolya_message(0x0F, 2); // ВРК=0x0F
            send_uvm_request(&request);
            // 4. Выдать состояние линии
            request.message = create_vydat_sostoyanie_linii_message(3);
            send_uvm_request(&request);
        }
    }
    wait_for_outstanding_sends(); // Ждем отправки всех сообщений подготовки

    // TODO: Добавить чтение ответов на запросы подготовки из uvm_incoming_response_queue

    printf("\n--- Подготовка к сеансу съемки ---\n");
     for (int i = 0; i < num_svms_in_config; ++i) {
        pthread_mutex_lock(&uvm_links_mutex);
        bool should_send = (svm_links[i].status == UVM_LINK_ACTIVE);
        pthread_mutex_unlock(&uvm_links_mutex);
        if (should_send) {
             request.target_svm_id = i;
             // Отправка параметров в зависимости от режима
             if (mode == MODE_DR) {
                 // TODO: Заполнить реальными данными
                 PrinyatParametrySdrBody sdr_body = {0}; sdr_body.pp_nl=1; sdr_body.brl=7; /*...*/
                 request.message = create_prinyat_parametry_sdr_message(&sdr_body, NULL, 4); // NULL для HRR
                 send_uvm_request(&request);
                 PrinyatParametryTsdBodyBase tsd_body = {0}; tsd_body.nin=1; /*...*/
                 request.message = create_prinyat_parametry_tsd_message(&tsd_body, NULL,NULL,NULL, 5); // NULL для массивов
                 send_uvm_request(&request);
             } else if (mode == MODE_OR || mode == MODE_OR1 || mode == MODE_VR) {
                 // TODO: Заполнить реальными данными
                 PrinyatParametrySoBody so_body = {0}; so_body.pp = mode; so_body.brl = 7; /*...*/
                 request.message = create_prinyat_parametry_so_message(&so_body, 4);
                 send_uvm_request(&request);
                 PrinyatParametry3TsoBody tso_body = {0}; tso_body.Ncadr=4; /*...*/
                 request.message = create_prinyat_parametry_3tso_message(&tso_body, 5);
                 send_uvm_request(&request);
                  if (mode == MODE_OR || mode == MODE_OR1) {
                      // PrinyatTimeRefRangeBody time_ref_body = {0}; /*...*/
                      // request.message = create_prinyat_time_ref_range_message(&time_ref_body, 7);
                      // send_uvm_request(&request);
                      // PrinyatReperBody reper_body = {0}; /*...*/
                      // request.message = create_prinyat_reper_message(&reper_body, 8);
                      // send_uvm_request(&request);
                  }
             }
             // Навигационные данные для всех режимов
             NavigatsionnyeDannyeBody nav_body = {0}; nav_body.mnd[0]=i; // Пример данных
             request.message = create_navigatsionnye_dannye_message(&nav_body, 6);
             send_uvm_request(&request);
        }
     }
    wait_for_outstanding_sends(); // Ждем отправки всех сообщений подготовки
    printf("UVM: Все сообщения подготовки к съемке отправлены.\n");

    // --- Ожидание ответов и завершения ---
    printf("UVM: Ожидание асинхронных сообщений от SVM (или Ctrl+C для завершения)...\n");
    UvmResponseMessage response;
    while (uvm_keep_running) {
        if (uvq_dequeue(uvm_incoming_response_queue, &response)) {
            printf("UVM Main: Received message type %u from SVM ID %d (Num %u)\n",
                   response.message.header.message_type,
                   response.source_svm_id,
                   get_full_message_number(&response.message.header));
            // TODO: Детальная обработка ответа в контексте response.source_svm_id
            // Например, проверка LAK:
             pthread_mutex_lock(&uvm_links_mutex);
             if (response.source_svm_id >= 0 && response.source_svm_id < MAX_SVM_CONFIGS) {
                  if (response.message.header.address != LOGICAL_ADDRESS_UVM) { // Адрес УВМ
                       fprintf(stderr, "Warning: Message from SVM %d has incorrect UVM address 0x%02X\n",
                               response.source_svm_id, response.message.header.address);
                  }
                  // Логический адрес отправителя (LAK) можно извлечь из тела некоторых сообщений
                  // if (response.message.header.message_type == MESSAGE_TYPE_CONFIRM_INIT) {
                  //    ConfirmInitBody *body = (ConfirmInitBody*)response.message.body;
                  //    if (body->lak != svm_links[response.source_svm_id].assigned_lak) { ... }
                  // }
             }
             pthread_mutex_unlock(&uvm_links_mutex);

        } else {
            // Очередь закрыта
            if (!uvm_keep_running) {
                printf("UVM Main: Response queue shut down. Exiting loop.\n");
                break;
            }
            // Ложное пробуждение?
            usleep(10000);
        }
    }

cleanup_threads:
    printf("UVM: Инициируем завершение потоков...\n");
    uvm_keep_running = false; // Убедимся, что флаг установлен
    // Сигналим очередям
    if (uvm_outgoing_request_queue) {
        // Отправляем команду SHUTDOWN сендеру
        UvmRequest shutdown_req = {.type = UVM_REQ_SHUTDOWN};
        queue_req_enqueue(uvm_outgoing_request_queue, &shutdown_req);
        // Дополнительно закрываем очередь, чтобы он точно проснулся
        queue_req_shutdown(uvm_outgoing_request_queue);
    }
    if (uvm_incoming_response_queue) uvq_shutdown(uvm_incoming_response_queue);

    // Сигналим условию ожидания
    pthread_mutex_lock(&uvm_send_counter_mutex);
    uvm_outstanding_sends = 0;
    pthread_cond_signal(&uvm_all_sent_cond);
    pthread_mutex_unlock(&uvm_send_counter_mutex);

    // Закрываем сокеты, чтобы разбудить Receiver'ов
    pthread_mutex_lock(&uvm_links_mutex);
    for(int i=0; i<num_svms_in_config; ++i) {
        if(svm_links[i].connection_handle >= 0) {
             shutdown(svm_links[i].connection_handle, SHUT_RDWR);
        }
    }
    pthread_mutex_unlock(&uvm_links_mutex);


    // Ожидаем завершения потоков
    printf("UVM: Ожидание завершения потоков...\n");
    if (sender_tid != 0) pthread_join(sender_tid, NULL);
    printf("UVM: Sender thread joined.\n");
    for (int i = 0; i < num_svms_in_config; ++i) {
        if (svm_links[i].receiver_tid != 0) {
            pthread_join(svm_links[i].receiver_tid, NULL);
            printf("UVM: Receiver thread for SVM ID %d joined.\n", i);
        }
    }

cleanup_connections:
    printf("UVM: Завершение работы и очистка ресурсов...\n");
    // Закрываем соединения и уничтожаем интерфейсы
    pthread_mutex_lock(&uvm_links_mutex);
    for (int i = 0; i < num_svms_in_config; ++i) {
        if (svm_links[i].io_handle) {
            if (svm_links[i].connection_handle >= 0) {
                svm_links[i].io_handle->disconnect(svm_links[i].io_handle, svm_links[i].connection_handle);
                printf("UVM: Connection for SVM ID %d closed.\n", i);
            }
            svm_links[i].io_handle->destroy(svm_links[i].io_handle);
             printf("UVM: IO Interface for SVM ID %d destroyed.\n", i);
            svm_links[i].io_handle = NULL;
            svm_links[i].connection_handle = -1;
            svm_links[i].status = UVM_LINK_INACTIVE;
        }
    }
    pthread_mutex_unlock(&uvm_links_mutex);

cleanup_queues:
    // Уничтожаем очереди
    if (uvm_outgoing_request_queue) queue_req_destroy(uvm_outgoing_request_queue);
    if (uvm_incoming_response_queue) uvq_destroy(uvm_incoming_response_queue);

    pthread_mutex_destroy(&uvm_links_mutex);
    pthread_mutex_destroy(&uvm_send_counter_mutex); // Уничтожаем мьютекс счетчика
    pthread_cond_destroy(&uvm_all_sent_cond);     // Уничтожаем условие

    printf("UVM: Очистка завершена.\n");
    return 0;
}