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
#include <sys/socket.h> // Для shutdown
#include <arpa/inet.h> // Для htons/htonl, inet_ntop

#include "../config/config.h"
#include "../io/io_interface.h"
#include "../protocol/protocol_defs.h"
#include "../protocol/message_builder.h"
#include "../protocol/message_utils.h"
#include "../utils/ts_queue_req.h"     // Очередь запросов к Sender'у
#include "../utils/ts_uvm_resp_queue.h" // Новая очередь ответов от Receiver'ов
#include "uvm_types.h"
#include "uvm_utils.h"

// --- Глобальные переменные ---
AppConfig config;
UvmSvmLink svm_links[MAX_SVM_INSTANCES];
pthread_mutex_t uvm_links_mutex;

ThreadSafeReqQueue *uvm_outgoing_request_queue = NULL;
ThreadSafeUvmRespQueue *uvm_incoming_response_queue = NULL;

volatile bool uvm_keep_running = true;
volatile int uvm_outstanding_sends = 0;
pthread_cond_t uvm_all_sent_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t uvm_send_counter_mutex = PTHREAD_MUTEX_INITIALIZER;

// Прототипы потоков
void* uvm_sender_thread_func(void* arg);
void* uvm_receiver_thread_func(void* arg);

// Обработчик сигналов
void uvm_handle_shutdown_signal(int sig) {
    (void)sig;
    const char msg[] = "\nUVM: Received shutdown signal. Exiting...\n";
    ssize_t written __attribute__((unused)) = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    uvm_keep_running = false;
    if (uvm_outgoing_request_queue) queue_req_shutdown(uvm_outgoing_request_queue);
    if (uvm_incoming_response_queue) uvq_shutdown(uvm_incoming_response_queue);
    pthread_mutex_lock(&uvm_send_counter_mutex);
    uvm_outstanding_sends = 0;
    pthread_cond_signal(&uvm_all_sent_cond);
    pthread_mutex_unlock(&uvm_send_counter_mutex);
}

// Функция отправки запроса Sender'у
bool send_uvm_request(UvmRequest *request) {
    if (!uvm_outgoing_request_queue || !request) return false;
    if (request->type == UVM_REQ_SEND_MESSAGE) {
        pthread_mutex_lock(&uvm_send_counter_mutex);
        uvm_outstanding_sends++;
        pthread_mutex_unlock(&uvm_send_counter_mutex);
    }
    if (!queue_req_enqueue(uvm_outgoing_request_queue, request)) {
        fprintf(stderr, "UVM Main: Failed to enqueue request (type %d)\n", request->type);
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
        //printf("UVM Main: Waiting for %d outstanding sends...\n", uvm_outstanding_sends);
        struct timespec wait_time; // Используем timedwait для предотвращения вечного ожидания
        clock_gettime(CLOCK_REALTIME, &wait_time);
        wait_time.tv_sec += 1; // Ждем 1 секунду
        pthread_cond_timedwait(&uvm_all_sent_cond, &uvm_send_counter_mutex, &wait_time);
    }
    if (uvm_outstanding_sends == 0) {
         //printf("UVM Main: All outstanding messages sent or processed.\n");
    } else if (uvm_keep_running) {
         fprintf(stderr,"UVM Main: Warning: Exiting wait loop with %d outstanding sends (timeout?).\n", uvm_outstanding_sends);
         uvm_outstanding_sends = 0; // Сбрасываем счетчик, чтобы не застрять
    } else {
         //printf("UVM Main: Exiting wait loop due to shutdown signal (outstanding: %d).\n", uvm_outstanding_sends);
         uvm_outstanding_sends = 0; // Сбрасываем счетчик
    }
    pthread_mutex_unlock(&uvm_send_counter_mutex);
}

// Основная функция
int main(int argc, char *argv[]) {
    pthread_t sender_tid = 0;
    int active_svm_count = 0;
    // Определяем переменную mode ЗДЕСЬ
    RadarMode mode = MODE_DR; // По умолчанию ДР

    // --- Парсинг аргумента командной строки для выбора режима ---
    if (argc > 1) {
        printf("DEBUG UVM: Got mode argument: %s\n", argv[1]);
        if (strcasecmp(argv[1], "OP") == 0) {
             mode = MODE_OR; // <-- Присваиваем значение ЗДЕСЬ
        } else if (strcasecmp(argv[1], "OR1") == 0) {
             mode = MODE_OR1; // <-- Присваиваем значение ЗДЕСЬ
        } else if (strcasecmp(argv[1], "DR") == 0) {
             mode = MODE_DR; // <-- Присваиваем значение ЗДЕСЬ
        } else if (strcasecmp(argv[1], "VR") == 0) {
             mode = MODE_VR; // <-- Присваиваем значение ЗДЕСЬ
        } else {
             printf("UVM: Warning: Unknown mode '%s'. Using default DR.\n", argv[1]);
             mode = MODE_DR; // Явно ставим дефолт
        }
    } else {
         printf("DEBUG UVM: No mode argument provided. Using default DR.\n");
         mode = MODE_DR; // Убедимся, что дефолт установлен
    }
    // Отладочный вывод СРАЗУ ПОСЛЕ парсинга
    printf("DEBUG UVM: Effective RadarMode selected: %d\n", mode); // Проверяем значение mode

    // --- Инициализация ---
    if (pthread_mutex_init(&uvm_links_mutex, NULL) != 0) {
        perror("UVM: Failed to initialize links mutex");
        exit(EXIT_FAILURE);
    }
    // Инициализация массива svm_links
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        svm_links[i].id = i;
        svm_links[i].io_handle = NULL;
        svm_links[i].connection_handle = -1;
        svm_links[i].status = UVM_LINK_INACTIVE;
        svm_links[i].receiver_tid = 0;
        svm_links[i].assigned_lak = 0;
        svm_links[i].last_activity_time = 0; // Инициализация времени
    }

    printf("UVM: Загрузка конфигурации...\n");
    if (load_config("config.ini", &config) != 0) { exit(EXIT_FAILURE); }
    int num_svms_in_config = config.num_svm_configs_found;
    printf("UVM: Found %d SVM configurations in config file.\n", num_svms_in_config);
    if (num_svms_in_config == 0) {
        fprintf(stderr, "UVM: No SVM configurations found in config.ini. Exiting.\n");
        exit(EXIT_FAILURE);
    }

    uvm_outgoing_request_queue = queue_req_create(50);
    uvm_incoming_response_queue = uvq_create(50 * num_svms_in_config);
    if (!uvm_outgoing_request_queue || !uvm_incoming_response_queue) {
        fprintf(stderr, "UVM: Failed to create message queues.\n");
        goto cleanup_queues;
    }

    signal(SIGINT, uvm_handle_shutdown_signal);
    signal(SIGTERM, uvm_handle_shutdown_signal);

    // --- Подключение к SVM ---
    printf("UVM: Connecting to SVMs...\n");
    printf("DEBUG UVM: IP before loop: %s\n", config.uvm_ethernet_target.target_ip); // Отладочный вывод
    pthread_mutex_lock(&uvm_links_mutex);
    for (int i = 0; i < num_svms_in_config; ++i) {
         if (!config.svm_config_loaded[i]) continue; // Пропускаем, если конфиг не найден

         printf("UVM: Attempting to connect to SVM ID %d (IP: %s, Port: %d)...\n",
                i, config.uvm_ethernet_target.target_ip, config.svm_ethernet[i].port);

         svm_links[i].status = UVM_LINK_CONNECTING;
         svm_links[i].assigned_lak = config.svm_settings[i].lak;

         EthernetConfig current_svm_config = {0};
         strncpy(current_svm_config.target_ip,
                 config.uvm_ethernet_target.target_ip,
                 sizeof(current_svm_config.target_ip) - 1);
         current_svm_config.target_ip[sizeof(current_svm_config.target_ip) - 1] = '\0'; // Гарантируем нуль-терминацию
         current_svm_config.port = config.svm_ethernet[i].port;
         current_svm_config.base.type = IO_TYPE_ETHERNET;

         printf("DEBUG UVM: Preparing to create interface for SVM %d with IP=%s, Port=%d\n",
                 i, current_svm_config.target_ip, current_svm_config.port); // Отладочный вывод

         svm_links[i].io_handle = create_ethernet_interface(&current_svm_config);
         if (!svm_links[i].io_handle) {
             fprintf(stderr, "UVM: Failed to create IO interface for SVM ID %d.\n", i);
             svm_links[i].status = UVM_LINK_FAILED;
             continue;
         }

         svm_links[i].connection_handle = svm_links[i].io_handle->connect(svm_links[i].io_handle);
         if (svm_links[i].connection_handle < 0) {
             fprintf(stderr, "UVM: Failed to connect to SVM ID %d (IP: %s, Port: %d).\n",
                     i, current_svm_config.target_ip, current_svm_config.port);
             svm_links[i].status = UVM_LINK_FAILED;
             svm_links[i].io_handle->destroy(svm_links[i].io_handle);
             svm_links[i].io_handle = NULL;
         } else {
             printf("UVM: Successfully connected to SVM ID %d (Handle: %d).\n", i, svm_links[i].connection_handle);
             svm_links[i].status = UVM_LINK_ACTIVE;
			 svm_links[i].last_activity_time = time(NULL); // <-- ИНИЦИАЛИЗИРОВАТЬ ВРЕМЯ
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
                svm_links[i].status = UVM_LINK_FAILED;
                // Не останавливаем все, попробуем работать с остальными
            }
        }
    }
    pthread_mutex_unlock(&uvm_links_mutex);
    printf("UVM: Потоки запущены. Режим работы: %d\n", mode); // Используем mode, а не его строковое представление

    // --- Основная логика UVM ---
    UvmRequest request;
    request.type = UVM_REQ_SEND_MESSAGE;
    uint16_t msg_counters[MAX_SVM_INSTANCES] = {0};

    printf("\n--- Подготовка к сеансу наблюдения ---\n");
    for (int i = 0; i < num_svms_in_config; ++i) {
        pthread_mutex_lock(&uvm_links_mutex);
        bool should_send = (svm_links[i].status == UVM_LINK_ACTIVE);
        LogicalAddress current_lak = svm_links[i].assigned_lak;
        pthread_mutex_unlock(&uvm_links_mutex);

        if (should_send) {
            request.target_svm_id = i;
            uint16_t current_msg_num = msg_counters[i]; // Берем текущий номер для этого SVM

            // 1. Инициализация канала
            request.message = create_init_channel_message(LOGICAL_ADDRESS_UVM_VAL, current_lak, current_msg_num++);
            InitChannelBody *init_body = (InitChannelBody *)request.message.body;
            init_body->lauvm = LOGICAL_ADDRESS_UVM_VAL;
            init_body->lak = current_lak;
            send_uvm_request(&request);

            // 2. Провести контроль
            request.message = create_provesti_kontrol_message(current_lak, 0x01, current_msg_num++);
            ProvestiKontrolBody *pk_body = (ProvestiKontrolBody *)request.message.body;
            pk_body->tk = 0x01;
            request.message.header.body_length = htons(sizeof(ProvestiKontrolBody));
            send_uvm_request(&request);

            // 3. Выдать результаты контроля
            request.message = create_vydat_rezultaty_kontrolya_message(current_lak, 0x0F, current_msg_num++);
            VydatRezultatyKontrolyaBody *vrk_body = (VydatRezultatyKontrolyaBody *)request.message.body;
            vrk_body->vrk = 0x0F;
            request.message.header.body_length = htons(sizeof(VydatRezultatyKontrolyaBody));
            send_uvm_request(&request);

            // 4. Выдать состояние линии
            request.message = create_vydat_sostoyanie_linii_message(current_lak, current_msg_num++);
            send_uvm_request(&request);

            msg_counters[i] = current_msg_num; // Сохраняем инкрементированный номер
        }
    }
    wait_for_outstanding_sends();

    printf("\n--- Подготовка к сеансу съемки ---\n");
    printf("DEBUG UVM: Mode before sending parameters: %d\n", mode); // <-- ЕЩЕ ОДНА ПРОВЕРКА
     for (int i = 0; i < num_svms_in_config; ++i) {
        pthread_mutex_lock(&uvm_links_mutex);
        bool should_send = (svm_links[i].status == UVM_LINK_ACTIVE);
        LogicalAddress current_lak = svm_links[i].assigned_lak;
        pthread_mutex_unlock(&uvm_links_mutex);

        if (should_send) {
             request.target_svm_id = i;
             uint16_t current_msg_num = msg_counters[i];

             // Отправка параметров в зависимости от режима
             if (mode == MODE_DR) { // <-- Условие проверяет mode
                 printf("UVM Main (SVM %d): Sending DR parameters...\n", i);
                 request.message = create_prinyat_parametry_sdr_message(current_lak, current_msg_num++);
                 PrinyatParametrySdrBodyBase sdr_body_base = {0}; sdr_body_base.pp_nl=(uint8_t)mode|1; sdr_body_base.q=htons(1500);
                 memcpy(request.message.body, &sdr_body_base, sizeof(sdr_body_base));
                 request.message.header.body_length = htons(sizeof(sdr_body_base));
                 send_uvm_request(&request);

                 request.message = create_prinyat_parametry_tsd_message(current_lak, current_msg_num++);
                 PrinyatParametryTsdBodyBase tsd_body_base = {0}; tsd_body_base.nin=htons(100); tsd_body_base.nout=htons(100);
                 memcpy(request.message.body, &tsd_body_base, sizeof(tsd_body_base));
                 request.message.header.body_length = htons(sizeof(tsd_body_base));
                 send_uvm_request(&request);

             } else if (mode == MODE_OR || mode == MODE_OR1) { // <-- Условие проверяет mode
                 printf("UVM Main (SVM %d): Sending OR/OR1 parameters...\n", i);
                 request.message = create_prinyat_parametry_so_message(current_lak, current_msg_num++);
                 PrinyatParametrySoBody so_body = {0}; so_body.pp=mode; so_body.knk=htons(400);
                 memcpy(request.message.body, &so_body, sizeof(so_body));
                 request.message.header.body_length = htons(sizeof(so_body));
                 send_uvm_request(&request);

                 request.message = create_prinyat_parametry_3tso_message(current_lak, current_msg_num++);
                 PrinyatParametry3TsoBody tso_body = {0}; tso_body.Ncadr=htons(1024);
                 memcpy(request.message.body, &tso_body, sizeof(tso_body));
                 request.message.header.body_length = htons(sizeof(tso_body));
                 send_uvm_request(&request);

                 request.message = create_prinyat_time_ref_range_message(current_lak, current_msg_num++);
                 PrinyatTimeRefRangeBody time_ref_body = {0};
                 memcpy(request.message.body, &time_ref_body, sizeof(time_ref_body));
                 request.message.header.body_length = htons(sizeof(time_ref_body));
                 send_uvm_request(&request);

                 request.message = create_prinyat_reper_message(current_lak, current_msg_num++);
                 PrinyatReperBody reper_body = {0}; reper_body.NTSO1=htons(1);
                 memcpy(request.message.body, &reper_body, sizeof(reper_body));
                 request.message.header.body_length = htons(sizeof(reper_body));
                 send_uvm_request(&request);

             } else if (mode == MODE_VR) { // <-- Условие проверяет mode
                 printf("UVM Main (SVM %d): Sending VR parameters...\n", i);
                 request.message = create_prinyat_parametry_so_message(current_lak, current_msg_num++);
                 PrinyatParametrySoBody so_body = {0}; so_body.pp=mode; so_body.knk=htons(500);
                 memcpy(request.message.body, &so_body, sizeof(so_body));
                 request.message.header.body_length = htons(sizeof(so_body));
                 send_uvm_request(&request);

                 request.message = create_prinyat_parametry_3tso_message(current_lak, current_msg_num++);
                 PrinyatParametry3TsoBody tso_body = {0}; tso_body.Ncadr=htons(512);
                 memcpy(request.message.body, &tso_body, sizeof(tso_body));
                 request.message.header.body_length = htons(sizeof(tso_body));
                 send_uvm_request(&request);
             }

             // Навигационные данные для всех режимов
             printf("UVM Main (SVM %d): Sending NAV data...\n", i);
             request.message = create_navigatsionnye_dannye_message(current_lak, current_msg_num++);
             NavigatsionnyeDannyeBody nav_body = {0}; nav_body.mnd[0]=i;
             memcpy(request.message.body, &nav_body, sizeof(nav_body));
             request.message.header.body_length = htons(sizeof(nav_body));
             send_uvm_request(&request);

             msg_counters[i] = current_msg_num; // Сохраняем инкрементированный номер
        }
     }
    wait_for_outstanding_sends();
    printf("UVM: Все сообщения подготовки к съемке отправлены.\n");

/*
	printf("\n--- Тест Отправки Дополнительных Команд ---\n");
	int extra_commands_to_send = 10; // Отправим еще 10 команд каждому
	for (int cmd_count = 0; cmd_count < extra_commands_to_send; ++cmd_count) {
		if (!uvm_keep_running) break; // Выходим, если пришел сигнал
		printf("Sending extra command batch %d/%d...\n", cmd_count + 1, extra_commands_to_send);
		for (int i = 0; i < num_svms_in_config; ++i) {
			pthread_mutex_lock(&uvm_links_mutex);
			bool should_send = (svm_links[i].status == UVM_LINK_ACTIVE);
			LogicalAddress current_lak = svm_links[i].assigned_lak;
			pthread_mutex_unlock(&uvm_links_mutex);

			if (should_send) {
				request.target_svm_id = i;
				// Отправляем "Выдать состояние линии"
				request.message = create_vydat_sostoyanie_linii_message(current_lak, msg_counters[i]++); // Инкрементируем номер
				send_uvm_request(&request);
			}
		}
		wait_for_outstanding_sends(); // Ждем отправки этой пачки
		sleep(1); // Небольшая пауза между пачками
	}
	printf("--- Конец Теста Отправки Дополнительных Команд ---\n");
*/


    // --- Ожидание ответов и завершения ---
    printf("UVM: Ожидание асинхронных сообщений от SVM (или Ctrl+C для завершения)...\n");
    UvmResponseMessage response;
    while (uvm_keep_running) {

        bool message_received_in_this_iteration = false; // Флаг для паузы

        // Пытаемся извлечь сообщение из очереди ответов
        if (uvq_dequeue(uvm_incoming_response_queue, &response)) {
            message_received_in_this_iteration = true; // Сообщение получено
            int svm_id = response.source_svm_id;
            Message *msg = &response.message;
            uint16_t msg_num = get_full_message_number(&msg->header);

            // Получаем ожидаемый LAK и текущий статус ПОД МЬЮТЕКСОМ
            LogicalAddress expected_lak = 0;
            UvmLinkStatus current_status = UVM_LINK_INACTIVE;
            pthread_mutex_lock(&uvm_links_mutex);
            if (svm_id >= 0 && svm_id < MAX_SVM_INSTANCES) { // Проверка валидности ID
                 expected_lak = svm_links[svm_id].assigned_lak;
                 current_status = svm_links[svm_id].status;
                 // Обновляем время активности при получении сообщения
                 if(current_status == UVM_LINK_ACTIVE) {
                     svm_links[svm_id].last_activity_time = time(NULL);
                 }
            }
            pthread_mutex_unlock(&uvm_links_mutex);

            // Игнорируем сообщения от неактивных линков
            if (current_status != UVM_LINK_ACTIVE) {
                printf("UVM Main: Ignored message type %u from non-active SVM ID %d (Status: %d)\n",
                       msg->header.message_type, svm_id, current_status);
                continue; // Переходим к следующей итерации цикла while
            }

            // Логируем получение сообщения
            printf("UVM Main: Received message type %u from SVM ID %d (Expected LAK 0x%02X, Num %u)\n",
                   msg->header.message_type, svm_id, expected_lak, msg_num);

            // --- Обработка ошибочных и ожидаемых ответов ---
            bool message_handled = false;

            // 1. Проверка на "Предупреждение"
            if (msg->header.message_type == MESSAGE_TYPE_PREDUPREZHDENIE) {
                message_handled = true;
                PreduprezhdenieBody *warn_body = (PreduprezhdenieBody*)msg->body;
                uint32_t bcb_host = ntohl(warn_body->bcb); // Преобразуем порядок байт
                fprintf(stderr, "\n!!! UVM: WARNING received from SVM ID %d (LAK 0x%02X) !!!\n", svm_id, warn_body->lak);
                fprintf(stderr, "  Event Type (TKS): %u\n", warn_body->tks);
                // TODO: Декодировать и вывести warn_body->pks (6 байт параметров)
                fprintf(stderr, "  BCB: 0x%08X\n", bcb_host);
                // Реакция: пока только логируем
                // TODO: Определить реакцию в зависимости от TKS (например, пометить линк FAILED)
                // pthread_mutex_lock(&uvm_links_mutex);
                // svm_links[svm_id].status = UVM_LINK_FAILED;
                // pthread_mutex_unlock(&uvm_links_mutex);
            }

            // 3. Проверка содержимого для ожидаемых ответов
            if (!message_handled) {
                 switch (msg->header.message_type) {
                    case MESSAGE_TYPE_CONFIRM_INIT: {
                        ConfirmInitBody *body = (ConfirmInitBody*)msg->body;
                        uint32_t bcb_host = ntohl(body->bcb);
                        printf("  Confirm Init: LAK=0x%02X, BCB=0x%08X\n", body->lak, bcb_host);
                        pthread_mutex_lock(&uvm_links_mutex);
                        if (svm_id >= 0 && svm_id < MAX_SVM_INSTANCES) {
                             if (body->lak != svm_links[svm_id].assigned_lak) {
                                  fprintf(stderr, "UVM: ERROR! LAK mismatch for SVM ID %d. Expected 0x%02X, Got 0x%02X\n",
                                          svm_id, svm_links[svm_id].assigned_lak, body->lak);
                                  svm_links[svm_id].status = UVM_LINK_FAILED;
                                  if (svm_links[svm_id].connection_handle >= 0) {
                                       shutdown(svm_links[svm_id].connection_handle, SHUT_RDWR);
                                  }
                             } else {
                                  printf("  LAK confirmed for SVM ID %d.\n", svm_id);
                             }
                        }
                        pthread_mutex_unlock(&uvm_links_mutex);
                        message_handled = true;
                        break;
                    }
                    case MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA: {
                         PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody*)msg->body;
                         uint32_t bcb_host = ntohl(body->bcb);
                         printf("  Control Confirmation: LAK=0x%02X, TK=0x%02X, BCB=0x%08X\n",
                                body->lak, body->tk, bcb_host);
                         // TODO: Проверить, соответствует ли body->tk отправленному запросу
                         message_handled = true;
                         break;
                    }
                    case MESSAGE_TYPE_RESULTATY_KONTROLYA: {
                         RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody*)msg->body;
                         uint16_t vsk_host = ntohs(body->vsk);
                         uint32_t bcb_host = ntohl(body->bcb);
                         printf("  Control Results: LAK=0x%02X, RSK=0x%02X, VSK=%ums, BCB=0x%08X\n",
                                body->lak, body->rsk, vsk_host, bcb_host);
                         if (body->rsk != 0x3F) { // 0x3F - пример "все ОК"
                              fprintf(stderr, "UVM: WARNING! Control failed/partially failed for SVM ID %d (LAK 0x%02X). RSK = 0x%02X\n",
                                      svm_id, body->lak, body->rsk);
                              // TODO: Определить реакцию (например, пометить WARNING/FAILED)
                         }
                         message_handled = true;
                         break;
                    }
                    case MESSAGE_TYPE_SOSTOYANIE_LINII: {
                        SostoyanieLiniiBody *body = (SostoyanieLiniiBody *)msg->body;
                        uint16_t kla_host = ntohs(body->kla);
                        uint32_t sla_host = ntohl(body->sla);
                        uint16_t ksa_host = ntohs(body->ksa);
                        uint32_t bcb_host = ntohl(body->bcb);
                         printf("  Line Status: LAK=0x%02X, KLA=%u, SLA=%u (x 1/100us), KSA=%u, BCB=0x%08X\n",
                                body->lak, kla_host, sla_host, ksa_host, bcb_host);
                        message_handled = true;
                        break;
                    }
                    // Обработка сообщений данных (СУБК, КО и т.д.) - пока просто логируем
                    case MESSAGE_TYPE_SUBK:
                    case MESSAGE_TYPE_KO:
                    case MESSAGE_TYPE_NK:
                    case MESSAGE_TYPE_RO:
                    // Добавить другие типы данных, если нужно
                         printf("  Received data message type %u from SVM %d\n", msg->header.message_type, svm_id);
                         message_handled = true;
                         break;

                    default:
                         // Тип не обрабатывается в switch
                         break;
                } // end switch
            } // end if (!message_handled)

            if (!message_handled) {
                printf("  Received unhandled or unexpected message type %u from SVM ID %d.\n", msg->header.message_type, svm_id);
            }

        } else { // Очередь пуста или закрыта
            if (!uvm_keep_running) {
                printf("UVM Main: Response queue shut down. Exiting loop.\n");
                break;
            }
            // Очередь пуста, message_received_in_this_iteration = false
        }

        // --- Периодическая проверка Keep-Alive ---
        time_t now_keepalive = time(NULL);
        const time_t keepalive_timeout = 60; // Таймаут молчания 60 секунд
        for (int k = 0; k < num_svms_in_config; ++k) { // Проверяем все настроенные SVM
             pthread_mutex_lock(&uvm_links_mutex);
             if (svm_links[k].status == UVM_LINK_ACTIVE &&
                 (now_keepalive - svm_links[k].last_activity_time) > keepalive_timeout)
             {
                  fprintf(stderr, "UVM Main: Keep-Alive TIMEOUT for SVM ID %d (no activity for %ld seconds)!\n",
                          k, keepalive_timeout);
                  svm_links[k].status = UVM_LINK_FAILED; // Помечаем как сбойный
                  if (svm_links[k].connection_handle >= 0) {
                       shutdown(svm_links[k].connection_handle, SHUT_RDWR); // Закрываем соединение
                       // close() будет вызван в cleanup_connections
                  }
             }
             pthread_mutex_unlock(&uvm_links_mutex);
        }

        // Небольшая пауза, если не было сообщений, чтобы не грузить CPU
        if (!message_received_in_this_iteration && uvm_keep_running) {
            usleep(100000); // 100 мс
        }

    } // end while (uvm_keep_running)

cleanup_connections:
    printf("UVM: Завершение работы и очистка ресурсов...\n");
    // Закрываем соединения и уничтожаем интерфейсы
    pthread_mutex_lock(&uvm_links_mutex);
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) { // Проверяем все слоты
        if (svm_links[i].io_handle) {
            if (svm_links[i].connection_handle >= 0) {
                svm_links[i].io_handle->disconnect(svm_links[i].io_handle, svm_links[i].connection_handle);
                // printf("UVM: Connection for SVM ID %d closed.\n", i); // Уже не нужно, т.к. Receiver завершился
            }
            svm_links[i].io_handle->destroy(svm_links[i].io_handle);
             // printf("UVM: IO Interface for SVM ID %d destroyed.\n", i);
            svm_links[i].io_handle = NULL;
            svm_links[i].connection_handle = -1;
            svm_links[i].status = UVM_LINK_INACTIVE;
            svm_links[i].receiver_tid = 0; // Сбрасываем ID потока
        }
    }
    pthread_mutex_unlock(&uvm_links_mutex);

cleanup_queues:
    // Уничтожаем очереди
    if (uvm_outgoing_request_queue) queue_req_destroy(uvm_outgoing_request_queue);
    if (uvm_incoming_response_queue) uvq_destroy(uvm_incoming_response_queue);

    // Уничтожаем мьютексы и условие
    pthread_mutex_destroy(&uvm_links_mutex);
    pthread_mutex_destroy(&uvm_send_counter_mutex);
    pthread_cond_destroy(&uvm_all_sent_cond);

    printf("UVM: Очистка завершена.\n");
    return 0;
}