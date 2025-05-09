/*
 * uvm/uvm_main.c
 * Описание: Основной файл UVM: инициализация, управление несколькими
 * соединениями с SVM, создание потоков, отправка запросов, обработка ответов.
 * ДОБАВЛЕН TCP-сервер для отправки статуса в GUI.
 * ОБНОВЛЕНО: Инициализация и обновление полей UvmSvmLink для GUI.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h> // Для shutdown
#include <arpa/inet.h>  // Для htons/htonl, inet_ntop
#include <netdb.h>      // Для getaddrinfo etc. (для gui_server)
#include <errno.h>      // Для errno

#include "../config/config.h"
#include "../io/io_interface.h"
#include "../protocol/protocol_defs.h"
#include "../protocol/message_builder.h"
#include "../protocol/message_utils.h"
#include "../utils/ts_queue_req.h"
#include "../utils/ts_uvm_resp_queue.h"
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

// Для GUI сервера
pthread_t gui_server_tid = 0;
int gui_listen_fd = -1;
int gui_client_fd = -1;
pthread_mutex_t gui_socket_mutex = PTHREAD_MUTEX_INITIALIZER;

// Прототипы потоков
void* uvm_sender_thread_func(void* arg);
void* uvm_receiver_thread_func(void* arg);
void* gui_server_thread(void* arg); // Поток для GUI

// Обработчик сигналов
void uvm_handle_shutdown_signal(int sig) {
    (void)sig;
    const char msg[] = "\nUVM: Received shutdown signal. Exiting...\n";
    ssize_t written __attribute__((unused)) = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    uvm_keep_running = false;
    // Сигналим очередям
    if (uvm_outgoing_request_queue) queue_req_shutdown(uvm_outgoing_request_queue);
    if (uvm_incoming_response_queue) uvq_shutdown(uvm_incoming_response_queue);
    // Сигналим условию ожидания
    pthread_mutex_lock(&uvm_send_counter_mutex);
    uvm_outstanding_sends = 0;
    pthread_cond_broadcast(&uvm_all_sent_cond); // Используем broadcast
    pthread_mutex_unlock(&uvm_send_counter_mutex);
    // Закрываем сокеты GUI
    pthread_mutex_lock(&gui_socket_mutex);
    if (gui_listen_fd >= 0) { int fd=gui_listen_fd; gui_listen_fd=-1; shutdown(fd, SHUT_RDWR); close(fd); }
    if (gui_client_fd >= 0) { int fd=gui_client_fd; gui_client_fd=-1; shutdown(fd, SHUT_RDWR); close(fd); }
    pthread_mutex_unlock(&gui_socket_mutex);
    // Закрываем сокеты SVM (делается в main при cleanup)
}

// Функция отправки запроса Sender'у
bool send_uvm_request(UvmRequest *request) {
    if (!uvm_outgoing_request_queue || !request) return false;
    if (request->type == UVM_REQ_SEND_MESSAGE) {
        pthread_mutex_lock(&uvm_links_mutex);
        if (request->target_svm_id >= 0 && request->target_svm_id < MAX_SVM_INSTANCES) {
            if (svm_links[request->target_svm_id].status == UVM_LINK_ACTIVE) {
                 svm_links[request->target_svm_id].last_sent_msg_type = request->message.header.message_type;
                 svm_links[request->target_svm_id].last_sent_msg_num = get_full_message_number(&request->message.header);
                 // *************** ИЗМЕНЕНИЕ: Обновление счетчика для имитации отключения ***************
                 if (svm_links[request->target_svm_id].simulating_disconnect_by_svm &&
                     svm_links[request->target_svm_id].svm_disconnect_countdown > 0) {
                      svm_links[request->target_svm_id].svm_disconnect_countdown--;
                 }
                 // **********************************************************************************
            }
        }
        pthread_mutex_unlock(&uvm_links_mutex);

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
        struct timespec wait_time;
        clock_gettime(CLOCK_REALTIME, &wait_time);
        wait_time.tv_sec += 1;
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

// --- Функция Потока GUI Сервера ---
void* gui_server_thread(void* arg) {
    (void)arg;
    int listen_port = 12345; // Порт для подключения GUI
    struct sockaddr_in serv_addr, cli_addr;
    socklen_t cli_len = sizeof(cli_addr);

    printf("GUI Server: Starting listener on port %d\n", listen_port);

    gui_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (gui_listen_fd < 0) { perror("GUI Server: socket"); return NULL; }

    int opt = 1;
    setsockopt(gui_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(listen_port);

    if (bind(gui_listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("GUI Server: bind failed"); close(gui_listen_fd); gui_listen_fd = -1; return NULL;
    }
    if (listen(gui_listen_fd, 1) < 0) {
        perror("GUI Server: listen failed"); close(gui_listen_fd); gui_listen_fd = -1; return NULL;
    }

    printf("GUI Server: Waiting for GUI connection on port %d...\n", listen_port);

    while (uvm_keep_running) {
        int current_client = -1; // Локальная переменная для текущего клиента
        int lfd = gui_listen_fd;
        if (lfd < 0) break;

        int new_client_fd = accept(lfd, (struct sockaddr*)&cli_addr, &cli_len);

        if (new_client_fd < 0) {
            if (!uvm_keep_running || errno == EBADF) {
                 printf("GUI Server: Accept loop interrupted or socket closed.\n");
            } else if (errno == EINTR) {
                 printf("GUI Server: accept() interrupted, retrying...\n");
                 continue;
            } else {
                if(uvm_keep_running) perror("GUI Server: Accept failed");
            }
            break;
        }

        char client_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr, client_ip_str, sizeof(client_ip_str));
        printf("GUI Server: Accepted connection from %s:%u (FD %d)\n",
               client_ip_str, ntohs(cli_addr.sin_port), new_client_fd);

        pthread_mutex_lock(&gui_socket_mutex);
        if (gui_client_fd >= 0) {
            printf("GUI Server: Closing previous GUI connection (FD %d)\n", gui_client_fd);
            close(gui_client_fd);
        }
        gui_client_fd = new_client_fd;
        current_client = gui_client_fd; // Устанавливаем для этого цикла
        pthread_mutex_unlock(&gui_socket_mutex);

        // Цикл отправки данных этому GUI клиенту
        while (uvm_keep_running && current_client >= 0) {
            char status_buffer[2048] = ""; // Увеличим буфер для большего количества полей
            char temp_buffer[512];      // Увеличим временный буфер

            // Формируем строку статуса
            pthread_mutex_lock(&uvm_links_mutex);
            for (int i = 0; i < config.num_svm_configs_found; ++i) {
                 if (!config.svm_config_loaded[i]) continue;

                 // *************** ОБНОВЛЕННЫЙ ФОРМАТ СТРОКИ ДЛЯ IPC ***************
                 snprintf(temp_buffer, sizeof(temp_buffer),
                          "ID:%d;Status:%d;LAK:%d;"
                          "SentType:%d;SentNum:%d;"
                          "RecvType:%d;RecvNum:%d;"
                          "BCB:%u;RSK:%u;WarnTKS:%u;"
                          "Timeout:%d;LAKFail:%d;CtrlFail:%d;"
                          "SimDisc:%d;DiscCnt:%d|",
                          i,
                          (int)svm_links[i].status,
                          svm_links[i].assigned_lak,
                          svm_links[i].last_sent_msg_type,
                          svm_links[i].last_sent_msg_num,
                          svm_links[i].last_recv_msg_type,
                          svm_links[i].last_recv_msg_num,
                          svm_links[i].last_recv_bcb,
                          svm_links[i].last_control_rsk,
                          svm_links[i].last_warning_tks,
                          svm_links[i].timeout_detected ? 1 : 0,
                          svm_links[i].lak_mismatch_detected ? 1 : 0,
                          svm_links[i].control_failure_flag ? 1 : 0,
                          svm_links[i].simulating_disconnect_by_svm ? 1 : 0,
                          svm_links[i].svm_disconnect_countdown
                         );
                 // *****************************************************************

                 if (strlen(status_buffer) + strlen(temp_buffer) < sizeof(status_buffer) - 1) {
                      strcat(status_buffer, temp_buffer);
                 } else {
                      fprintf(stderr, "GUI Server: Status buffer overflow!\n");
                      break;
                 }
            }
            pthread_mutex_unlock(&uvm_links_mutex);

            if (strlen(status_buffer) > 0) {
                 status_buffer[strlen(status_buffer) - 1] = '\n'; // Заменяем последний '|' на '\n'
            } else {
                 strcpy(status_buffer, "NoActiveSVMs\n"); // Если нет активных, отправляем это
            }

            // Отправляем статус клиенту GUI
            pthread_mutex_lock(&gui_socket_mutex);
            current_client = gui_client_fd; // Перепроверяем хэндл на случай, если он изменился
            if (current_client >= 0) {
                ssize_t sent = send(current_client, status_buffer, strlen(status_buffer), MSG_NOSIGNAL);
                if (sent <= 0) {
                    if (sent < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                        // Ошибка не фатальная, попробуем позже
                    } else {
                         if (sent == 0) printf("GUI Server: GUI client (FD %d) closed connection gracefully.\n", current_client);
                         else perror("GUI Server: send failed");
                         close(current_client);
                         gui_client_fd = -1;   // Сбрасываем глобальный
                         current_client = -1; // Выходим из внутреннего цикла
                    }
                }
            }
            pthread_mutex_unlock(&gui_socket_mutex);

            // Пауза перед следующей отправкой
            for(int t=0; t<10 && uvm_keep_running && current_client >=0; ++t) {
                 usleep(100000); // 10 * 100мс = 1 секунда
            }
        } // end while send loop (для текущего клиента)
        if (current_client == -1) { // Если соединение было разорвано
             printf("GUI Server: Disconnected from GUI client.\n");
        }
         // Возвращаемся к accept для нового клиента, если uvm_keep_running все еще true
    } // end while main loop (uvm_keep_running)

    printf("GUI Server: Thread shutting down.\n");
    // Закрываем сокеты, если они еще открыты (на случай если вышли не через uvm_handle_shutdown_signal)
    pthread_mutex_lock(&gui_socket_mutex);
     if (gui_listen_fd >= 0) { close(gui_listen_fd); gui_listen_fd = -1;}
     if (gui_client_fd >= 0) { close(gui_client_fd); gui_client_fd = -1;}
    pthread_mutex_unlock(&gui_socket_mutex);

    return NULL;
}

// Основная функция
int main(int argc, char *argv[]) {
    pthread_t sender_tid = 0;
    int active_svm_count = 0;
    // Определяем переменную mode ЗДЕСЬ
    RadarMode mode = MODE_DR; // По умолчанию ДР

    // --- Парсинг аргументов командной строки ---
    // Проходим по всем аргументам, начиная с argv[1]
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--wait-for-gui") == 0) {
            waitForGui = true;
        } else if (strcasecmp(argv[i], "OR") == 0) {
            mode = MODE_OR;
        } else if (strcasecmp(argv[i], "OR1") == 0) {
            mode = MODE_OR1;
        } else if (strcasecmp(argv[i], "DR") == 0) {
            mode = MODE_DR;
        } else if (strcasecmp(argv[i], "VR") == 0) {
            mode = MODE_VR;
        } else {
            // Если аргумент не распознан как флаг или известный режим,
            // можно вывести предупреждение или проигнорировать.
            // Для простоты пока будем игнорировать неизвестные аргументы,
            // кроме тех, что похожи на режимы, но не совпали по регистру (strcasecmp это покроет).
            // Если это был первый аргумент и он не совпал ни с чем, mode останется дефолтным (DR).
            if (i == 1) { // Если это первый аргумент и он не режим и не флаг
                 printf("UVM: Warning: Unknown mode argument '%s'. Using default DR.\n", argv[i]);
            } else {
                 printf("UVM: Warning: Unknown argument '%s'. Ignored.\n", argv[i]);
            }
        }
    }

    if (waitForGui) {
        printf("UVM: Option --wait-for-gui enabled. Will wait for GUI connection.\n");
    }
    // Если аргумент режима не был передан, mode останется MODE_DR (установлен по умолчанию)
    if (argc <= 1 && !waitForGui) { // Если вообще нет аргументов
         printf("DEBUG UVM: No mode argument provided. Using default DR.\n");
    } else if (argc > 1 && !waitForGui && mode == MODE_DR && (strcasecmp(argv[1], "DR") !=0) && (strcmp(argv[1],"--wait-for-gui") !=0 ) ) {
        // Если был один аргумент, он не --wait-for-gui, и mode все еще DR (значит, аргумент не был OR, OR1, VR)
    }


    printf("DEBUG UVM: Effective RadarMode selected: %d\n", mode);
    // --- Конец блока парсинга ---

    // --- Инициализация ---
    if (pthread_mutex_init(&uvm_links_mutex, NULL) != 0) {
        perror("UVM: Failed to initialize links mutex");
        exit(EXIT_FAILURE);
    }
	if (pthread_mutex_init(&gui_socket_mutex, NULL) != 0) { 
		perror("UVM: Failed to initialize GUI mutex");
		exit(EXIT_FAILURE); 
	} // Инициализация мьютекса GUI

    // Инициализация массива svm_links
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) { // Используем MAX_SVM_INSTANCES
        svm_links[i].id = i;
        svm_links[i].io_handle = NULL;
        svm_links[i].connection_handle = -1;
        svm_links[i].status = UVM_LINK_INACTIVE;
        svm_links[i].receiver_tid = 0;
        svm_links[i].assigned_lak = 0;
        svm_links[i].last_activity_time = 0;
        svm_links[i].last_sent_msg_type = (MessageType)0; // Или какой-то Invalid тип
        svm_links[i].last_sent_msg_num = 0;
        svm_links[i].last_recv_msg_type = (MessageType)0;
        svm_links[i].last_recv_msg_num = 0;
        svm_links[i].last_recv_bcb = 0;
        svm_links[i].last_control_rsk = 0xFF; // Признак "не было"
        svm_links[i].last_warning_tks = 0;    // 0 - нет предупреждения
        svm_links[i].last_warning_time = 0;
        svm_links[i].timeout_detected = false;
        svm_links[i].lak_mismatch_detected = false;
        svm_links[i].control_failure_flag = false;
        svm_links[i].simulating_disconnect_by_svm = false; // По умолчанию SVM не имитирует дисконнект
        svm_links[i].svm_disconnect_countdown = -1;     // Выключено
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
    for (int i = 0; i < num_svms_in_config; ++i) { // Итерируем по num_svms_in_config
         if (!config.svm_config_loaded[i]) continue;

         printf("UVM: Attempting to connect to SVM ID %d (IP: %s, Port: %d)...\n",
                i, config.uvm_ethernet_target.target_ip, config.svm_ethernet[i].port);

         svm_links[i].status = UVM_LINK_CONNECTING;
         svm_links[i].assigned_lak = config.svm_settings[i].lak; // LAK из конфига SVM
         // *************** ИНИЦИАЛИЗАЦИЯ ПАРАМЕТРОВ ИМИТАЦИИ ОТКЛЮЧЕНИЯ SVM ***************
         // Эти параметры читаются из конфига SVM, uvm_app их знает и может отобразить
         svm_links[i].simulating_disconnect_by_svm = (config.svm_settings[i].disconnect_after_messages > 0);
         if (svm_links[i].simulating_disconnect_by_svm) {
             svm_links[i].svm_disconnect_countdown = config.svm_settings[i].disconnect_after_messages;
         } else {
             svm_links[i].svm_disconnect_countdown = -1;
         }

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
    printf("UVM: Запуск потоков Sender, Receiver(s) и GUI Server...\n");
    if (pthread_create(&sender_tid, NULL, uvm_sender_thread_func, NULL) != 0) {
        perror("UVM: Failed to create sender thread");
        goto cleanup_connections; // Или другая метка очистки
    }

    // Запускаем Receiver'ы для активных соединений
    pthread_mutex_lock(&uvm_links_mutex);
    for (int i = 0; i < num_svms_in_config; ++i) {
        if (svm_links[i].status == UVM_LINK_ACTIVE) {
            if (pthread_create(&svm_links[i].receiver_tid, NULL, uvm_receiver_thread_func, &svm_links[i]) != 0) {
                perror("UVM: Failed to create receiver thread");
                svm_links[i].status = UVM_LINK_FAILED; // Помечаем как ошибку
                // Можно добавить логику остановки уже запущенных потоков, если это критично
            }
        }
    }
    pthread_mutex_unlock(&uvm_links_mutex);

    // Запуск GUI сервера
    if (pthread_create(&gui_server_tid, NULL, gui_server_thread, NULL) != 0) {
        perror("UVM: Failed to create GUI server thread");
        gui_server_tid = 0; // Сбрасываем ID, чтобы не пытаться его join'ить
                            // Не фатально, можно продолжить без GUI
    }
    // --- Конец запуска потоков ---


    // *************** УСЛОВНОЕ ОЖИДАНИЕ GUI ***************
    if (waitForGui) {
        printf("UVM: Waiting for GUI client to connect on port 12345 (or press Ctrl+C)...\n");
        while (uvm_keep_running) { // Проверяем глобальный флаг завершения
            pthread_mutex_lock(&gui_socket_mutex);
            bool gui_connected = (gui_client_fd >= 0);
            pthread_mutex_unlock(&gui_socket_mutex);

            if (gui_connected) {
                printf("UVM: GUI client connected! Proceeding with SVM operations.\n");
                break; // GUI подключился, выходим из ожидания
            }

            // Короткая пауза с проверкой флага, чтобы не зависнуть и быстро отреагировать на Ctrl+C
            bool still_running_check = false;
            for (int k=0; k<5; ++k) { // Проверяем каждые 100мс, в сумме 0.5с
                if (!uvm_keep_running) {
                    still_running_check = true;
                    break;
                }
                usleep(100000); // 0.1 секунды
            }
            if (still_running_check) break; // Выходим, если uvm_keep_running стал false
        }
        if (!uvm_keep_running && gui_client_fd < 0) { // Если вышли по Ctrl+C во время ожидания GUI
             printf("UVM: Shutdown signaled while waiting for GUI.\n");
             goto cleanup_threads; // Переход к завершению всех потоков
        }
    }
    // ******************************************************

    // Теперь эта строка точно после возможного ожидания GUI
    printf("UVM: All necessary threads started. Selected RadarMode: %d\n", mode);

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

/******************************* Тест Отправки Дополнительных Команд *******************************
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
*************************** Конец Теста Отправки Дополнительных Команд ***************************/


    // --- Ожидание ответов и Keep-Alive / Таймауты ---
    printf("UVM: Ожидание асинхронных сообщений от SVM (или Ctrl+C для завершения)...\n");
    UvmResponseMessage response;
    while (uvm_keep_running) {
        bool message_received_in_this_iteration = false;

        if (uvq_dequeue(uvm_incoming_response_queue, &response)) {
            message_received_in_this_iteration = true;
            int svm_id = response.source_svm_id;
            Message *msg = &response.message;
            uint16_t msg_num = get_full_message_number(&msg->header);

            LogicalAddress expected_lak = 0;
            UvmLinkStatus current_status = UVM_LINK_INACTIVE;

            // --- Обновление данных в svm_links[svm_id] ---
            pthread_mutex_lock(&uvm_links_mutex);
            if (svm_id >= 0 && svm_id < MAX_SVM_INSTANCES) { // Используем MAX_SVM_INSTANCES
                 expected_lak = svm_links[svm_id].assigned_lak;
                 current_status = svm_links[svm_id].status;
                 if(current_status == UVM_LINK_ACTIVE) {
                     svm_links[svm_id].last_activity_time = time(NULL);
                     svm_links[svm_id].last_recv_msg_type = msg->header.message_type;
                     svm_links[svm_id].last_recv_msg_num = msg_num;
                     // *************** ОБНОВЛЕНИЕ СПЕЦИФИЧНЫХ ПОЛЕЙ ДЛЯ GUI ***************
                     switch(msg->header.message_type) {
                         case MESSAGE_TYPE_CONFIRM_INIT:
                              if(ntohs(msg->header.body_length) >= sizeof(ConfirmInitBody)) {
                                   svm_links[svm_id].last_recv_bcb = ntohl(((ConfirmInitBody*)msg->body)->bcb);
                                   if (((ConfirmInitBody*)msg->body)->lak != svm_links[svm_id].assigned_lak) {
                                        svm_links[svm_id].lak_mismatch_detected = true;
                                   } else {
                                        svm_links[svm_id].lak_mismatch_detected = false; // Сбрасываем, если совпал
                                   }
                              }
                              break;
                         case MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA:
                              if(ntohs(msg->header.body_length) >= sizeof(PodtverzhdenieKontrolyaBody)) {
                                   svm_links[svm_id].last_recv_bcb = ntohl(((PodtverzhdenieKontrolyaBody*)msg->body)->bcb);
                              }
                              break;
                         case MESSAGE_TYPE_PREDUPREZHDENIE:
                              if(ntohs(msg->header.body_length) >= sizeof(PreduprezhdenieBody)) {
                                   PreduprezhdenieBody* warn_body = (PreduprezhdenieBody*)msg->body;
                                   svm_links[svm_id].last_recv_bcb = ntohl(warn_body->bcb);
                                   svm_links[svm_id].last_warning_tks = warn_body->tks;
                                   svm_links[svm_id].last_warning_time = time(NULL);
                              }
                              break;
                         case MESSAGE_TYPE_RESULTATY_KONTROLYA:
                              if(ntohs(msg->header.body_length) >= sizeof(RezultatyKontrolyaBody)) {
                                   RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody*)msg->body;
                                   svm_links[svm_id].last_control_rsk = body->rsk;
                                   svm_links[svm_id].last_recv_bcb = ntohl(body->bcb);
                                   svm_links[svm_id].control_failure_flag = (body->rsk != 0x3F); // Пример "ОК"
                              }
                              break;
                         case MESSAGE_TYPE_SOSTOYANIE_LINII:
                              if(ntohs(msg->header.body_length) >= sizeof(SostoyanieLiniiBody)) {
                                   SostoyanieLiniiBody *body = (SostoyanieLiniiBody*)msg->body;
                                   // svm_links[svm_id].last_recv_kla = ntohs(body->kla); // Убрали для упрощения IPC
                                   // svm_links[svm_id].last_recv_sla_us100 = ntohl(body->sla);
                                   // svm_links[svm_id].last_recv_ksa = ntohs(body->ksa);
                                   svm_links[svm_id].last_recv_bcb = ntohl(body->bcb);
                              }
                              break;
                     }
                     // *********************************************************************
                 }
            }
            pthread_mutex_unlock(&uvm_links_mutex);
            // --- Конец обновления svm_links ---

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

 // end while (uvm_keep_running)
}

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
	pthread_mutex_destroy(&gui_socket_mutex); // <-- Уничтожаем мьютекс GUI

    printf("UVM: Очистка завершена.\n");
    return 0;
}