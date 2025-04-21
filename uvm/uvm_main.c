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
#include <arpa/inet.h> // Для htons/htonl (при заполнении тел)

#include "../config/config.h"
#include "../io/io_interface.h"
#include "../protocol/protocol_defs.h"
#include "../protocol/message_builder.h" // Используем прототипы отсюда
#include "../protocol/message_utils.h"
#include "../utils/ts_queue_req.h"     // Очередь запросов к Sender'у
#include "../utils/ts_uvm_resp_queue.h" // Новая очередь ответов от Receiver'ов
#include "uvm_types.h"
#include "uvm_utils.h" // Включаем созданный .h

// --- Глобальные переменные ---
AppConfig config;
UvmSvmLink svm_links[MAX_SVM_CONFIGS];
pthread_mutex_t uvm_links_mutex;

ThreadSafeReqQueue *uvm_outgoing_request_queue = NULL; // Исправлен тип
ThreadSafeUvmRespQueue *uvm_incoming_response_queue = NULL;

volatile bool uvm_keep_running = true;
volatile int uvm_outstanding_sends = 0;
pthread_cond_t uvm_all_sent_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t uvm_send_counter_mutex = PTHREAD_MUTEX_INITIALIZER;

// Прототипы потоков
void* uvm_sender_thread_func(void* arg);
void* uvm_receiver_thread_func(void* arg);

// Обработчик сигналов (без изменений)
void uvm_handle_shutdown_signal(int sig) {
    (void)sig; // Убираем предупреждение о неиспользуемом параметре
    const char msg[] = "\nUVM: Received shutdown signal. Exiting...\n";
    ssize_t written __attribute__((unused)) = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    uvm_keep_running = false;
    if (uvm_outgoing_request_queue) queue_req_shutdown(uvm_outgoing_request_queue); // Используем правильное имя типа
    if (uvm_incoming_response_queue) uvq_shutdown(uvm_incoming_response_queue);
    pthread_mutex_lock(&uvm_send_counter_mutex);
    uvm_outstanding_sends = 0;
    pthread_cond_signal(&uvm_all_sent_cond);
    pthread_mutex_unlock(&uvm_send_counter_mutex);
}

// Функция отправки запроса Sender'у (тип очереди исправлен)
bool send_uvm_request(UvmRequest *request) {
    if (!uvm_outgoing_request_queue || !request) return false;
    if (request->type == UVM_REQ_SEND_MESSAGE) {
        pthread_mutex_lock(&uvm_send_counter_mutex);
        uvm_outstanding_sends++;
        pthread_mutex_unlock(&uvm_send_counter_mutex);
    }
    if (!queue_req_enqueue(uvm_outgoing_request_queue, request)) { // Используем правильное имя типа
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

// Функция ожидания отправки всех сообщений (без изменений)
void wait_for_outstanding_sends(void) {
    pthread_mutex_lock(&uvm_send_counter_mutex);
    while (uvm_outstanding_sends > 0 && uvm_keep_running) {
        //printf("UVM Main: Waiting for %d outstanding sends...\n", uvm_outstanding_sends);
        pthread_cond_wait(&uvm_all_sent_cond, &uvm_send_counter_mutex);
    }
    //printf("UVM Main: Wait loop finished (outstanding: %d).\n", uvm_outstanding_sends);
    pthread_mutex_unlock(&uvm_send_counter_mutex);
}

// Основная функция
int main(int argc, char *argv[]) {
    pthread_t sender_tid = 0;
    int active_svm_count = 0;
    RadarMode mode = MODE_DR;
    if (argc > 1) { /* ... парсинг режима ... */ }

    if (pthread_mutex_init(&uvm_links_mutex, NULL) != 0) { /*...*/ exit(EXIT_FAILURE); }

    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) { /* ... инициализация svm_links ... */ }

    printf("UVM: Загрузка конфигурации...\n");
    if (load_config("config.ini", &config) != 0) { exit(EXIT_FAILURE); } // Используем вызов с 2 аргументами
    int num_svms_in_config = config.num_svm_configs_found;
    printf("UVM: Found %d SVM configurations in config file.\n", num_svms_in_config);
    if (num_svms_in_config == 0) { /*...*/ exit(EXIT_FAILURE); }

    uvm_outgoing_request_queue = queue_req_create(50); // Используем правильное имя типа
    uvm_incoming_response_queue = uvq_create(50 * num_svms_in_config);
    if (!uvm_outgoing_request_queue || !uvm_incoming_response_queue) { /*...*/ goto cleanup_queues; }

    signal(SIGINT, uvm_handle_shutdown_signal);
    signal(SIGTERM, uvm_handle_shutdown_signal);

	printf("UVM: Connecting to SVMs...\n");
	printf("DEBUG UVM: IP before loop: %s\n", config.uvm_ethernet_target.target_ip);
	pthread_mutex_lock(&uvm_links_mutex);
	for (int i = 0; i < num_svms_in_config; ++i) {
		 if (!config.svm_config_loaded[i]) continue;

		 printf("UVM: Attempting to connect to SVM ID %d (IP: %s, Port: %d)...\n",
				i, config.uvm_ethernet_target.target_ip, config.svm_ethernet[i].port);

		 svm_links[i].status = UVM_LINK_CONNECTING;
		 svm_links[i].assigned_lak = config.svm_settings[i].lak;

		 // --- Создаем конфигурацию для этого конкретного SVM ---
		 EthernetConfig current_svm_config = {0};
		 strncpy(current_svm_config.target_ip,
				 config.uvm_ethernet_target.target_ip, // Убедись, что здесь ПРАВИЛЬНОЕ поле
				 sizeof(current_svm_config.target_ip) - 1);
		 // current_svm_config.target_ip[sizeof(current_svm_config.target_ip) - 1] = '\0'; // strncpy не всегда добавляет null-терминатор! Добавим явно.

		 // Устанавливаем порт конкретного SVM
		 current_svm_config.port = config.svm_ethernet[i].port;
		 current_svm_config.base.type = IO_TYPE_ETHERNET;

		 // Добавим отладочный вывод ПЕРЕД созданием интерфейса
		 printf("DEBUG UVM: Preparing to create interface for SVM %d with IP=%s, Port=%d\n",
				i, current_svm_config.target_ip, current_svm_config.port);

		 // --- Создаем НОВЫЙ IO интерфейс ДЛЯ ЭТОГО ЛИНКА ---
		 svm_links[i].io_handle = create_ethernet_interface(&current_svm_config); // <-- Создаем здесь
		 if (!svm_links[i].io_handle) {
			 fprintf(stderr, "UVM: Failed to create IO interface for SVM ID %d.\n", i);
			 svm_links[i].status = UVM_LINK_FAILED;
			 continue; // Пропускаем этот линк
		 }

		 // Выполняем подключение, используя СОБСТВЕННЫЙ io_handle линка
		 svm_links[i].connection_handle = svm_links[i].io_handle->connect(svm_links[i].io_handle);
		 if (svm_links[i].connection_handle < 0) {
			 fprintf(stderr, "UVM: Failed to connect to SVM ID %d (IP: %s, Port: %d).\n",
					 i, current_svm_config.target_ip, current_svm_config.port);
			 svm_links[i].status = UVM_LINK_FAILED;
			 // Важно: Уничтожаем созданный интерфейс, если подключение не удалось
			 svm_links[i].io_handle->destroy(svm_links[i].io_handle);
			 svm_links[i].io_handle = NULL;
		 } else {
			 printf("UVM: Successfully connected to SVM ID %d (Handle: %d).\n", i, svm_links[i].connection_handle);
			 svm_links[i].status = UVM_LINK_ACTIVE;
			 active_svm_count++;
		 }
	} // end for
	pthread_mutex_unlock(&uvm_links_mutex);

    if (active_svm_count == 0) { /*...*/ goto cleanup_queues; }
    printf("UVM: Connected to %d out of %d configured SVMs.\n", active_svm_count, num_svms_in_config);

    printf("UVM: Запуск потоков Sender и Receiver(s)...\n");
    if (pthread_create(&sender_tid, NULL, uvm_sender_thread_func, NULL) != 0) { /*...*/ goto cleanup_connections; }
    pthread_mutex_lock(&uvm_links_mutex);
    for (int i = 0; i < num_svms_in_config; ++i) {
        if (svm_links[i].status == UVM_LINK_ACTIVE) {
            if (pthread_create(&svm_links[i].receiver_tid, NULL, uvm_receiver_thread_func, &svm_links[i]) != 0) { /*...*/ }
        }
    }
    pthread_mutex_unlock(&uvm_links_mutex);
    if (!uvm_keep_running) goto cleanup_threads;
    printf("UVM: Потоки запущены. Режим работы: %d\n", mode);

    // --- Основная логика UVM ---
    UvmRequest request;
    request.type = UVM_REQ_SEND_MESSAGE;
    uint16_t msg_num_init = 0, msg_num_kontrol = 1, msg_num_rezult = 2, msg_num_sost = 3;
    uint16_t msg_num_param1 = 4, msg_num_param2 = 5, msg_num_nav = 6;

    printf("\n--- Подготовка к сеансу наблюдения ---\n");
    for (int i = 0; i < num_svms_in_config; ++i) {
        pthread_mutex_lock(&uvm_links_mutex);
        bool should_send = (svm_links[i].status == UVM_LINK_ACTIVE);
        LogicalAddress current_lak = svm_links[i].assigned_lak; // Получаем LAK для передачи
        pthread_mutex_unlock(&uvm_links_mutex);

        if (should_send) {
            request.target_svm_id = i;
			// 1. Инициализация канала
			request.message = create_init_channel_message(LOGICAL_ADDRESS_UVM_VAL, current_lak, msg_num_init++);
			// Заполняем тело ПОСЛЕ вызова билдера
			InitChannelBody *init_body = (InitChannelBody *)request.message.body;
			init_body->lauvm = LOGICAL_ADDRESS_UVM_VAL; // Наш адрес UVM
			init_body->lak = current_lak;              // Адрес, который должен установить SVM
			// Длина тела уже установлена билдером через SET_HEADER
			send_uvm_request(&request);

            // 2. Провести контроль
            request.message = create_provesti_kontrol_message(current_lak, 0x01, msg_num_kontrol++); // TK=1
            // Заполняем тело здесь, так как билдер (по .h) его не заполняет
            ProvestiKontrolBody *pk_body = (ProvestiKontrolBody *)request.message.body;
            pk_body->tk = 0x01;
            // Обновляем длину тела (размер структуры тела)
            request.message.header.body_length = htons(sizeof(ProvestiKontrolBody));
            send_uvm_request(&request);

            // 3. Выдать результаты контроля
            request.message = create_vydat_rezultaty_kontrolya_message(current_lak, 0x0F, msg_num_rezult++); // ВРК=0x0F
            VydatRezultatyKontrolyaBody *vrk_body = (VydatRezultatyKontrolyaBody *)request.message.body;
            vrk_body->vrk = 0x0F;
            request.message.header.body_length = htons(sizeof(VydatRezultatyKontrolyaBody));
            send_uvm_request(&request);

            // 4. Выдать состояние линии
            request.message = create_vydat_sostoyanie_linii_message(current_lak, msg_num_sost++);
            // Тело пустое, длина 0 уже установлена билдером
            send_uvm_request(&request);
        }
    }
    wait_for_outstanding_sends();

    printf("\n--- Подготовка к сеансу съемки ---\n");
     for (int i = 0; i < num_svms_in_config; ++i) {
        pthread_mutex_lock(&uvm_links_mutex);
        bool should_send = (svm_links[i].status == UVM_LINK_ACTIVE);
        LogicalAddress current_lak = svm_links[i].assigned_lak;
        pthread_mutex_unlock(&uvm_links_mutex);

        if (should_send) {
             request.target_svm_id = i;
             if (mode == MODE_DR) {
                 printf("UVM Main (SVM %d): Sending DR parameters...\n", i);
                 // 1. Принять параметры СДР
                 request.message = create_prinyat_parametry_sdr_message(current_lak, msg_num_param1++);
                 PrinyatParametrySdrBodyBase sdr_body_base = {0};
                 // TODO: Заполнить sdr_body_base реальными или тестовыми данными для DR
                 sdr_body_base.pp_nl = (uint8_t)mode | 0x01; // Пример
                 sdr_body_base.q = htons(1500); // Пример
                 memcpy(request.message.body, &sdr_body_base, sizeof(PrinyatParametrySdrBodyBase));
                 request.message.header.body_length = htons(sizeof(PrinyatParametrySdrBodyBase));
                 send_uvm_request(&request);

                 // 2. Принять параметры ЦДР
                 request.message = create_prinyat_parametry_tsd_message(current_lak, msg_num_param2++);
                 PrinyatParametryTsdBodyBase tsd_body_base = {0};
                  // TODO: Заполнить tsd_body_base реальными или тестовыми данными для DR
                 tsd_body_base.nin = htons(100); // Пример
                 tsd_body_base.nout = htons(100); // Пример
                 memcpy(request.message.body, &tsd_body_base, sizeof(PrinyatParametryTsdBodyBase));
                 request.message.header.body_length = htons(sizeof(PrinyatParametryTsdBodyBase));
                 send_uvm_request(&request);

             } else if (mode == MODE_OR || mode == MODE_OR1) {
                 printf("UVM Main (SVM %d): Sending OR/OR1 parameters...\n", i);
                 // 1. Принять параметры СО
                 request.message = create_prinyat_parametry_so_message(current_lak, msg_num_param1++);
                 PrinyatParametrySoBody so_body = {0};
                 // TODO: Заполнить so_body реальными или тестовыми данными для OR/OR1
                 so_body.pp = mode;
                 so_body.knk = htons(400); // Другой KNK для примера
                 memcpy(request.message.body, &so_body, sizeof(PrinyatParametrySoBody));
                 request.message.header.body_length = htons(sizeof(PrinyatParametrySoBody));
                 send_uvm_request(&request);

                 // 2. Принять параметры 3ЦО
                 request.message = create_prinyat_parametry_3tso_message(current_lak, msg_num_param2++);
                 PrinyatParametry3TsoBody tso_body = {0};
                 // TODO: Заполнить tso_body реальными или тестовыми данными для OR/OR1
                 tso_body.Ncadr = htons(1024); // Пример
                 memcpy(request.message.body, &tso_body, sizeof(PrinyatParametry3TsoBody));
                 request.message.header.body_length = htons(sizeof(PrinyatParametry3TsoBody));
                 send_uvm_request(&request);

                 // 3. Принять TIME_REF_RANGE (только для OR/OR1)
                 request.message = create_prinyat_time_ref_range_message(current_lak, msg_num_param1++); // Используем счетчик param1 или новый
                 PrinyatTimeRefRangeBody time_ref_body = {0};
                 // TODO: Заполнить time_ref_body
                 memcpy(request.message.body, &time_ref_body, sizeof(PrinyatTimeRefRangeBody));
                 request.message.header.body_length = htons(sizeof(PrinyatTimeRefRangeBody));
                 send_uvm_request(&request);

                 // 4. Принять Reper (только для OR/OR1)
                 request.message = create_prinyat_reper_message(current_lak, msg_num_param2++); // Используем счетчик param2 или новый
                 PrinyatReperBody reper_body = {0};
                 // TODO: Заполнить reper_body
                 reper_body.NTSO1 = htons(1); reper_body.ReperR1 = htons(100); /* ... */
                 memcpy(request.message.body, &reper_body, sizeof(PrinyatReperBody));
                 request.message.header.body_length = htons(sizeof(PrinyatReperBody));
                 send_uvm_request(&request);

             } else if (mode == MODE_VR) {
                 printf("UVM Main (SVM %d): Sending VR parameters...\n", i);
                 // 1. Принять параметры СО
                 request.message = create_prinyat_parametry_so_message(current_lak, msg_num_param1++);
                 PrinyatParametrySoBody so_body = {0};
                 // TODO: Заполнить so_body реальными или тестовыми данными для VR
                 so_body.pp = mode;
                 so_body.knk = htons(500); // Другой KNK
                 memcpy(request.message.body, &so_body, sizeof(PrinyatParametrySoBody));
                 request.message.header.body_length = htons(sizeof(PrinyatParametrySoBody));
                 send_uvm_request(&request);

                 // 2. Принять параметры 3ЦО
                 request.message = create_prinyat_parametry_3tso_message(current_lak, msg_num_param2++);
                 PrinyatParametry3TsoBody tso_body = {0};
                 // TODO: Заполнить tso_body реальными или тестовыми данными для VR
                 tso_body.Ncadr = htons(512); // Другое значение
                 memcpy(request.message.body, &tso_body, sizeof(PrinyatParametry3TsoBody));
                 request.message.header.body_length = htons(sizeof(PrinyatParametry3TsoBody));
                 send_uvm_request(&request);

                 // Для VR не нужны TIME_REF и REPER
             }

             // Навигационные данные для всех режимов (отправляются после специфичных)
             printf("UVM Main (SVM %d): Sending NAV data...\n", i);
             request.message = create_navigatsionnye_dannye_message(current_lak, msg_num_nav++);
             NavigatsionnyeDannyeBody nav_body = {0}; nav_body.mnd[0]=i;
             memcpy(request.message.body, &nav_body, sizeof(NavigatsionnyeDannyeBody));
             request.message.header.body_length = htons(sizeof(NavigatsionnyeDannyeBody));
             send_uvm_request(&request);
        }
     }
    wait_for_outstanding_sends();
    printf("UVM: Все сообщения подготовки к съемке отправлены.\n");

    printf("UVM: Ожидание асинхронных сообщений от SVM (или Ctrl+C для завершения)...\n");
    UvmResponseMessage response;
    while (uvm_keep_running) {
        if (uvq_dequeue(uvm_incoming_response_queue, &response)) {
            printf("UVM Main: Received message type %u from SVM ID %d (Num %u, LAK 0x%02X)\n",
                   response.message.header.message_type,
                   response.source_svm_id,
                   get_full_message_number(&response.message.header),
                   response.message.header.address); // Адрес отправителя (SVM LAK) должен быть здесь

            // TODO: Детальная обработка ответа
            // Проверка адреса UVM в сообщении
             if (response.message.header.address != LOGICAL_ADDRESS_UVM_VAL) {
                   fprintf(stderr, "Warning: Message from SVM %d has incorrect UVM address 0x%02X\n",
                           response.source_svm_id, response.message.header.address);
              }
        } else {
            if (!uvm_keep_running) { /*...*/ break; }
            usleep(10000);
        }
    }

cleanup_threads:
    printf("UVM: Инициируем завершение потоков...\n");
    uvm_keep_running = false;
    if (uvm_outgoing_request_queue) {
        UvmRequest shutdown_req = {.type = UVM_REQ_SHUTDOWN};
        queue_req_enqueue(uvm_outgoing_request_queue, &shutdown_req); // Используем правильное имя
        queue_req_shutdown(uvm_outgoing_request_queue); // Используем правильное имя
    }
    if (uvm_incoming_response_queue) uvq_shutdown(uvm_incoming_response_queue);
    pthread_mutex_lock(&uvm_send_counter_mutex);
    uvm_outstanding_sends = 0;
    pthread_cond_signal(&uvm_all_sent_cond);
    pthread_mutex_unlock(&uvm_send_counter_mutex);
    pthread_mutex_lock(&uvm_links_mutex);
    for(int i=0; i<num_svms_in_config; ++i) {
        if(svm_links[i].connection_handle >= 0) { shutdown(svm_links[i].connection_handle, SHUT_RDWR); }
    }
    pthread_mutex_unlock(&uvm_links_mutex);

    printf("UVM: Ожидание завершения потоков...\n");
    if (sender_tid != 0) pthread_join(sender_tid, NULL);
    printf("UVM: Sender thread joined.\n");
    for (int i = 0; i < num_svms_in_config; ++i) {
        if (svm_links[i].receiver_tid != 0) {
            pthread_join(svm_links[i].receiver_tid, NULL);
            //printf("UVM: Receiver thread for SVM ID %d joined.\n", i);
        }
    }
    printf("UVM: All receiver threads joined.\n");


cleanup_connections:
    printf("UVM: Завершение работы и очистка ресурсов...\n");
	pthread_mutex_lock(&uvm_links_mutex);
	for (int i = 0; i < num_svms_in_config; ++i) { // Используем num_svms_in_config или MAX_SVM_CONFIGS
		if (svm_links[i].io_handle) { // Проверяем, что интерфейс был создан
			if (svm_links[i].connection_handle >= 0) {
				svm_links[i].io_handle->disconnect(svm_links[i].io_handle, svm_links[i].connection_handle);
				printf("UVM: Connection for SVM ID %d closed.\n", i);
			}
			svm_links[i].io_handle->destroy(svm_links[i].io_handle); // <-- Уничтожаем интерфейс
			 printf("UVM: IO Interface for SVM ID %d destroyed.\n", i);
			svm_links[i].io_handle = NULL; // Обнуляем указатель
			svm_links[i].connection_handle = -1;
			svm_links[i].status = UVM_LINK_INACTIVE;
		}
	}
	pthread_mutex_unlock(&uvm_links_mutex);

cleanup_queues:
    if (uvm_outgoing_request_queue) queue_req_destroy(uvm_outgoing_request_queue); // Правильное имя
    if (uvm_incoming_response_queue) uvq_destroy(uvm_incoming_response_queue);
    pthread_mutex_destroy(&uvm_links_mutex);
    pthread_mutex_destroy(&uvm_send_counter_mutex);
    pthread_cond_destroy(&uvm_all_sent_cond);
    printf("UVM: Очистка завершена.\n");
    return 0;
}