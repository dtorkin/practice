/*
 * uvm/uvm_main.c
 * ... (описание как раньше) ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

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
UvmSvmLink svm_links[MAX_SVM_INSTANCES]; // Используем MAX_SVM_INSTANCES
pthread_mutex_t uvm_links_mutex;

ThreadSafeReqQueue *uvm_outgoing_request_queue = NULL;
ThreadSafeUvmRespQueue *uvm_incoming_response_queue = NULL;

volatile bool uvm_keep_running = true;
volatile int uvm_outstanding_sends = 0;
pthread_cond_t uvm_all_sent_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t uvm_send_counter_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t gui_server_tid = 0;
int gui_listen_fd = -1;
int gui_client_fd = -1;
pthread_mutex_t gui_socket_mutex = PTHREAD_MUTEX_INITIALIZER;

// Прототипы
void* uvm_sender_thread_func(void* arg);
void* uvm_receiver_thread_func(void* arg);
void* gui_server_thread(void* arg);
void send_to_gui_socket(const char *message_to_gui); // Объявляем здесь

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

// Вспомогательная функция для отправки сообщения в GUI
// Вызывается из разных мест, где нужно уведомить GUI о событии
void send_to_gui_socket(const char *message_to_gui) {
    if (!uvm_keep_running) return; // Не отправляем, если уже завершаемся

    pthread_mutex_lock(&gui_socket_mutex);
    if (gui_client_fd >= 0) {
        // Добавляем \n, если его нет, т.к. GUI парсит по строкам
        char buffer_with_newline[1024]; // Достаточно большой буфер
        strncpy(buffer_with_newline, message_to_gui, sizeof(buffer_with_newline) - 2);
        buffer_with_newline[sizeof(buffer_with_newline) - 2] = '\0'; // Гарантируем null-терминацию
        // Если строка не пустая и не заканчивается на \n, добавляем его
        size_t len = strlen(buffer_with_newline);
        if (len > 0 && buffer_with_newline[len - 1] != '\n') {
            if (len < sizeof(buffer_with_newline) - 1) {
                buffer_with_newline[len] = '\n';
                buffer_with_newline[len + 1] = '\0';
            } else { // Буфер почти полон, просто заменяем последний символ
                buffer_with_newline[sizeof(buffer_with_newline) - 2] = '\n';
            }
        } else if (len == 0) { // Пустая строка, просто отправляем \n
             strcpy(buffer_with_newline, "\n");
        }


        ssize_t sent = send(gui_client_fd, buffer_with_newline, strlen(buffer_with_newline), MSG_NOSIGNAL);
        if (sent <= 0) {
            if (sent == 0) {
                printf("GUI Server: GUI client (FD %d) closed connection during send_to_gui_socket.\n", gui_client_fd);
            } else if (errno != EPIPE && errno != ECONNRESET) { // Игнорируем ошибки разрыва соединения здесь
                perror("GUI Server: send_to_gui_socket send failed");
            }
            // Закрываем и сбрасываем, если ошибка или закрытие
            close(gui_client_fd);
            gui_client_fd = -1;
        }
    }
    pthread_mutex_unlock(&gui_socket_mutex);
}

// Функция отправки запроса Sender'у
bool send_uvm_request(UvmRequest *request) {
    if (!uvm_outgoing_request_queue || !request) return false;
    bool success = true;
    char gui_msg_buffer[256]; // Для отправки в GUI

    if (request->type == UVM_REQ_SEND_MESSAGE) {
        pthread_mutex_lock(&uvm_links_mutex);
        if (request->target_svm_id >= 0 && request->target_svm_id < MAX_SVM_INSTANCES) { // Используем MAX_SVM_INSTANCES
            UvmSvmLink *link = &svm_links[request->target_svm_id]; // Получаем указатель на link
            if (link->status == UVM_LINK_ACTIVE) {
                 link->last_sent_msg_type = request->message.header.message_type;
                 link->last_sent_msg_num = get_full_message_number(&request->message.header);
                 link->last_sent_msg_time = time(NULL);

                 // --- ИСПРАВЛЕННЫЙ БЛОК ДЛЯ GUI ---
                 snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
                          "SENT;SVM_ID:%d;Type:%d;Num:%u;LAK:0x%02X", // Сообщение типа SENT
                          request->target_svm_id,                     // Используем target_svm_id
                          request->message.header.message_type,       // Тип отправляемого сообщения
                          link->last_sent_msg_num,                    // Номер отправляемого (уже обновлен)
                          link->assigned_lak);                        // LAK этого линка
                 send_to_gui_socket(gui_msg_buffer);
                 // --- КОНЕЦ ИСПРАВЛЕННОГО БЛОКА ---
            }
        }
        pthread_mutex_unlock(&uvm_links_mutex);

        pthread_mutex_lock(&uvm_send_counter_mutex);
        uvm_outstanding_sends++;
        pthread_mutex_unlock(&uvm_send_counter_mutex);
    }

    if (!queue_req_enqueue(uvm_outgoing_request_queue, request)) {
        fprintf(stderr, "UVM Main: Failed to enqueue request (type %s, target_svm_id %d)\n",
                uvm_request_type_to_message_name(request->type), request->target_svm_id);
        success = false;
        if (request->type == UVM_REQ_SEND_MESSAGE) {
             pthread_mutex_lock(&uvm_send_counter_mutex);
             if(uvm_outstanding_sends > 0) uvm_outstanding_sends--;
             pthread_mutex_unlock(&uvm_send_counter_mutex);
        }
    }
    return success;
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
    char gui_msg_buffer[1024]; // Буфер для отправки начального состояния

    printf("GUI Server: Starting listener on port %d\n", listen_port);

    gui_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (gui_listen_fd < 0) {
        perror("GUI Server: socket");
        return NULL;
    }

    int opt = 1;
    if (setsockopt(gui_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("GUI Server: setsockopt(SO_REUSEADDR) failed");
        // Не фатально, но может мешать быстрому перезапуску
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Слушаем на всех локальных IP
    serv_addr.sin_port = htons(listen_port);

    if (bind(gui_listen_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("GUI Server: bind failed");
        close(gui_listen_fd);
        gui_listen_fd = -1;
        return NULL;
    }

    if (listen(gui_listen_fd, 1) < 0) { // Очередь ожидания 1 клиент
        perror("GUI Server: listen failed");
        close(gui_listen_fd);
        gui_listen_fd = -1;
        return NULL;
    }

    printf("GUI Server: Waiting for GUI connection on port %d...\n", listen_port);

    while (uvm_keep_running) {
        int lfd_current = gui_listen_fd; // Копируем перед блокирующим вызовом
        if (lfd_current < 0) { // Если сокет был закрыт извне (например, сигналом)
            break;
        }
        int new_client_fd = accept(lfd_current, (struct sockaddr*)&cli_addr, &cli_len);

        if (new_client_fd < 0) {
            if (!uvm_keep_running || errno == EBADF) {
                 printf("GUI Server: Accept loop interrupted or socket closed.\n");
            } else if (errno == EINTR) {
                 // printf("GUI Server: accept() interrupted, retrying...\n");
                 continue; // Повторяем accept
            } else {
                if(uvm_keep_running) perror("GUI Server: Accept failed");
            }
            break; // Выходим из цикла при ошибке или завершении
        }

        char client_ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &cli_addr.sin_addr, client_ip_str, sizeof(client_ip_str));
        printf("GUI Server: Accepted connection from %s:%u (FD %d)\n",
               client_ip_str, ntohs(cli_addr.sin_port), new_client_fd);

        pthread_mutex_lock(&gui_socket_mutex);
        if (gui_client_fd >= 0) { // Если уже есть клиент GUI
            printf("GUI Server: Closing previous GUI connection (FD %d)\n", gui_client_fd);
            close(gui_client_fd);
        }
        gui_client_fd = new_client_fd; // Сохраняем новый дескриптор
        pthread_mutex_unlock(&gui_socket_mutex);

        // --- Отправка начального состояния всех SVM новому клиенту GUI ---
        printf("GUI Server: Sending initial state to new GUI client (FD %d).\n", new_client_fd);
        for (int i = 0; i < config.num_svm_configs_found; ++i) {
            if (!config.svm_config_loaded[i]) continue; // Пропускаем незагруженные

            pthread_mutex_lock(&uvm_links_mutex); // Блокируем доступ к svm_links
            // 1. Отправляем текущий статус линка
			snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
					 "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X", // <-- LAK здесь
					 i, svm_links[i].status, svm_links[i].assigned_lak);
			send_to_gui_socket(gui_msg_buffer);

            // 2. Отправляем информацию о последнем отправленном сообщении UVM
            if (svm_links[i].last_sent_msg_time > 0) { // Если было отправлено хоть что-то
                snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
                         "SENT;SVM_ID:%d;Type:%d;Num:%u;LAK:0x%02X",
                         i, svm_links[i].last_sent_msg_type, svm_links[i].last_sent_msg_num, svm_links[i].assigned_lak);
                send_to_gui_socket(gui_msg_buffer);
            }
            // 3. Отправляем информацию о последнем полученном сообщении от SVM
            if (svm_links[i].last_recv_msg_time > 0) { // Если было получено хоть что-то
                snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
                         "RECV;SVM_ID:%d;Type:%d;Num:%u;LAK:0x%02X", // LAK здесь - это адрес отправителя (SVM) из заголовка
                         i, svm_links[i].last_recv_msg_type, svm_links[i].last_recv_msg_num, svm_links[i].assigned_lak); // Используем assigned_lak для консистентности
                send_to_gui_socket(gui_msg_buffer);
            }
            // 4. Отправляем информацию об ошибках/предупреждениях (если были)
            if (svm_links[i].timeout_detected) {
                 snprintf(gui_msg_buffer, sizeof(gui_msg_buffer), "EVENT;SVM_ID:%d;Type:KeepAliveTimeout;Details:No activity", i);
                 send_to_gui_socket(gui_msg_buffer);
            }
            if (svm_links[i].response_timeout_detected) { // Если это поле будет
                 snprintf(gui_msg_buffer, sizeof(gui_msg_buffer), "EVENT;SVM_ID:%d;Type:ResponseTimeout;Details:Command timed out", i);
                 send_to_gui_socket(gui_msg_buffer);
            }
            if (svm_links[i].lak_mismatch_detected) {
                 snprintf(gui_msg_buffer, sizeof(gui_msg_buffer), "EVENT;SVM_ID:%d;Type:LAKMismatch;Details:LAK incorrect", i);
                 send_to_gui_socket(gui_msg_buffer);
            }
            if (svm_links[i].control_failure_detected) {
                 snprintf(gui_msg_buffer, sizeof(gui_msg_buffer), "EVENT;SVM_ID:%d;Type:ControlFail;Details:RSK=0x%02X", i, svm_links[i].last_control_rsk);
                 send_to_gui_socket(gui_msg_buffer);
            }
            if (svm_links[i].last_warning_tks != 0) {
                 snprintf(gui_msg_buffer, sizeof(gui_msg_buffer), "EVENT;SVM_ID:%d;Type:Warning;Details:TKS=%u", i, svm_links[i].last_warning_tks);
                 send_to_gui_socket(gui_msg_buffer);
            }
            pthread_mutex_unlock(&uvm_links_mutex);
        }
        printf("GUI Server: Initial state sent to GUI client (FD %d).\n", new_client_fd);
        // --- Конец отправки начального состояния ---

        // Этот поток теперь просто держит соединение с GUI открытым.
        // Данные будут отправляться в GUI из send_to_gui_socket(), вызываемой другими частями main.
        // Нам нужно как-то детектировать, что GUI клиент закрыл соединение.
        // Можно периодически пытаться отправить "пустое" сообщение (ping) или просто ждать ошибок send.
        while(uvm_keep_running) {
            // Проверяем, жив ли еще клиент, пытаясь что-то отправить (например, пустую строку раз в N сек)
            // или просто ждем, пока send_to_gui_socket() не вернет ошибку.
            // Для простоты, этот цикл будет просто спать. send_to_gui_socket() обработает разрыв.
            // Если gui_client_fd станет -1 (из-за ошибки в send_to_gui_socket), выходим.
            pthread_mutex_lock(&gui_socket_mutex);
            int current_client_fd_check = gui_client_fd;
            pthread_mutex_unlock(&gui_socket_mutex);
            if (current_client_fd_check != new_client_fd || current_client_fd_check < 0) { // Клиент изменился или закрылся
                 printf("GUI Server: GUI client (FD %d) seems to have disconnected or changed. Waiting for new one.\n", new_client_fd);
                 break; // Выходим из этого внутреннего цикла и возвращаемся к accept
            }
            sleep(1); // Спим, пока соединение с GUI активно
        }
        // Если вышли из внутреннего цикла, значит, соединение с GUI было разорвано
        // или uvm_keep_running стал false.
        printf("GUI Server: Loop for client FD %d ended.\n", new_client_fd);
        // Возвращаемся к accept для нового клиента GUI, если uvm_keep_running еще true
    } // end while main loop

    printf("GUI Server: Thread shutting down.\n");
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
	bool waitForGui = false;  // По умолчанию не ждем GUI

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
		pthread_mutex_destroy(&uvm_links_mutex);
		exit(EXIT_FAILURE);
	}

    // Инициализация svm_links
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        svm_links[i].id = i;
        svm_links[i].io_handle = NULL;
        svm_links[i].connection_handle = -1;
        svm_links[i].status = UVM_LINK_INACTIVE;
        svm_links[i].receiver_tid = 0;
		svm_links[i].prep_state = PREP_STATE_NOT_STARTED;
		svm_links[i].last_command_sent_time = 0;
		svm_links[i].current_preparation_msg_num = 0; // Начинаем с 0 для каждого SVM
		svm_links[i].last_sent_prep_cmd_type = 0;
        svm_links[i].assigned_lak = 0;
        svm_links[i].last_activity_time = 0;
        svm_links[i].last_sent_msg_type = (MessageType)0;
        svm_links[i].last_sent_msg_num = 0;
        svm_links[i].last_sent_msg_time = 0;
        svm_links[i].last_recv_msg_type = (MessageType)0;
        svm_links[i].last_recv_msg_num = 0;
        svm_links[i].last_recv_msg_time = 0;
        svm_links[i].last_recv_bcb = 0;
        svm_links[i].last_control_rsk = 0xFF;
        svm_links[i].last_warning_tks = 0;
        svm_links[i].last_warning_time = 0;
        svm_links[i].timeout_detected = false;
        svm_links[i].response_timeout_detected = false;
        svm_links[i].lak_mismatch_detected = false;
        svm_links[i].control_failure_detected = false;
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
			  // Отправка события LinkStatus в GUI
             char initial_gui_msg[128];
             snprintf(initial_gui_msg, sizeof(initial_gui_msg),
                      "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d", i, UVM_LINK_ACTIVE);
             send_to_gui_socket(initial_gui_msg);
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
             goto cleanup_connections; // Переход к завершению всех потоков
        }
    }
    // ******************************************************


    printf("UVM: All necessary threads started. Selected RadarMode: %d\n", mode);


    printf("UVM: Начало основного цикла управления SVM (Параллельная подготовка)...\n");
    UvmResponseMessage response_msg_data_main; // Для чтения из очереди ответов
    char gui_buffer_main_loop[512];          // Буфер для сообщений в GUI

    // Таймауты для разных ответов (в секундах)
    const time_t TIMEOUT_CONFIRM_INIT_S_MAIN = 5;
    const time_t TIMEOUT_CONFIRM_KONTROL_S_MAIN = 12; // Учитывая возможную задержку SVM (sleep(1)+sleep(10))
    const time_t TIMEOUT_RESULTS_KONTROL_S_MAIN = 8;  // SVM может "думать" до 6с + передача
    const time_t TIMEOUT_LINE_STATUS_S_MAIN = 5;

    bool all_svms_preparation_done_or_failed = false;

    while (uvm_keep_running && !all_svms_preparation_done_or_failed) {
        bool processed_in_iteration = false;
        all_svms_preparation_done_or_failed = true; // Предполагаем, что все завершили/отвалились

        // === БЛОК A: ПРОВЕРКА И ОТПРАВКА СЛЕДУЮЩЕЙ КОМАНДЫ ПОДГОТОВКИ ===
        pthread_mutex_lock(&uvm_links_mutex);
        for (int i = 0; i < num_svms_in_config; ++i) {
            if (!config.svm_config_loaded[i]) continue;
            UvmSvmLink *link = &svm_links[i];

            // Если SVM еще не завершил подготовку и не в ошибке, то предполагаем, что не все завершено
            if (link->status == UVM_LINK_ACTIVE &&
                link->prep_state != PREP_STATE_PREPARATION_COMPLETE &&
                link->prep_state != PREP_STATE_FAILED) {
                all_svms_preparation_done_or_failed = false; // Хотя бы один еще в процессе
            }

            if (link->status != UVM_LINK_ACTIVE) { // Работаем только с активными TCP-линками
                if (link->prep_state != PREP_STATE_FAILED && link->prep_state != PREP_STATE_NOT_STARTED) {
                    link->prep_state = PREP_STATE_FAILED; // Если TCP упал во время подготовки
                }
                continue;
            }

            UvmRequest request_prep_main; // Используем другое имя, чтобы не конфликтовать с глобальной request
            request_prep_main.type = UVM_REQ_SEND_MESSAGE;
            request_prep_main.target_svm_id = i;
            bool should_send_cmd_main = false;
            MessageType cmd_to_send_type = (MessageType)0; // Для логирования

            switch (link->prep_state) {
                case PREP_STATE_NOT_STARTED:
                    cmd_to_send_type = MESSAGE_TYPE_INIT_CHANNEL;
                    request_prep_main.message = create_init_channel_message(LOGICAL_ADDRESS_UVM_VAL, link->assigned_lak, link->current_preparation_msg_num);
                    InitChannelBody *init_b = (InitChannelBody*)request_prep_main.message.body;
                    init_b->lauvm = LOGICAL_ADDRESS_UVM_VAL; init_b->lak = link->assigned_lak;
                    should_send_cmd_main = true;
                    break;

                case PREP_STATE_AWAITING_CONFIRM_KONTROL: // Это состояние означает: ConfirmInit получен, ПОРА СЛАТЬ ProvestiKontrol
                    cmd_to_send_type = MESSAGE_TYPE_PROVESTI_KONTROL;
                    request_prep_main.message = create_provesti_kontrol_message(link->assigned_lak, 0x01, link->current_preparation_msg_num);
                    ProvestiKontrolBody* pk_b = (ProvestiKontrolBody*)request_prep_main.message.body; pk_b->tk = 0x01;
                    request_prep_main.message.header.body_length = htons(sizeof(ProvestiKontrolBody));
                    should_send_cmd_main = true;
                    break;

                case PREP_STATE_AWAITING_RESULTS_KONTROL: // PodtverzhdenieKontrolya получено, ПОРА СЛАТЬ VydatRezultaty
                    cmd_to_send_type = MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA;
                    request_prep_main.message = create_vydat_rezultaty_kontrolya_message(link->assigned_lak, 0x0F, link->current_preparation_msg_num);
                    VydatRezultatyKontrolyaBody* vrk_b = (VydatRezultatyKontrolyaBody*)request_prep_main.message.body; vrk_b->vrk = 0x0F;
                    request_prep_main.message.header.body_length = htons(sizeof(VydatRezultatyKontrolyaBody));
                    should_send_cmd_main = true;
                    break;

                case PREP_STATE_AWAITING_LINE_STATUS: // RezultatyKontrolya получены, ПОРА СЛАТЬ VydatSostoyanieLinii
                    cmd_to_send_type = MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII;
                    request_prep_main.message = create_vydat_sostoyanie_linii_message(link->assigned_lak, link->current_preparation_msg_num);
                    should_send_cmd_main = true;
                    break;

                // В состояниях ожидания ответа (_AWAITING_...) команды не отправляем из этого блока
                case PREP_STATE_AWAITING_CONFIRM_INIT:
                // Следующие состояния ожидания переименованы для ясности (см. ниже)
                // case PREP_STATE_AWAITING_PODTV_KONTROL_REPLY:
                // case PREP_STATE_AWAITING_REZ_KONTROL_REPLY:
                // case PREP_STATE_AWAITING_LINE_STATUS_REPLY:
                case PREP_STATE_PREPARATION_COMPLETE:
                case PREP_STATE_FAILED:
                    break; // Ничего не делаем
            }

            if (should_send_cmd_main) {
                printf("UVM Main (SVM %d): Отправка команды подготовки типа %u (Num %u). Состояние до: %d\n",
                       i, cmd_to_send_type, link->current_preparation_msg_num, link->prep_state);
                link->last_sent_prep_cmd_type = cmd_to_send_type; // Запоминаем тип отправленной команды

                if (send_uvm_request(&request_prep_main)) {
                    link->last_command_sent_time = time(NULL);
                    // Переводим в соответствующее состояние ОЖИДАНИЯ ответа
                    if (cmd_to_send_type == MESSAGE_TYPE_INIT_CHANNEL) {
                        link->prep_state = PREP_STATE_AWAITING_CONFIRM_INIT;
                    } else if (cmd_to_send_type == MESSAGE_TYPE_PROVESTI_KONTROL) {
                        link->prep_state = PREP_STATE_AWAITING_CONFIRM_KONTROL; // Ожидаем Подтверждение контроля
                    } else if (cmd_to_send_type == MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA) {
                        link->prep_state = PREP_STATE_AWAITING_RESULTS_KONTROL; // Ожидаем Результаты контроля
                    } else if (cmd_to_send_type == MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII) {
                        link->prep_state = PREP_STATE_AWAITING_LINE_STATUS; // Ожидаем Состояние линии
                    }
                    printf("UVM Main (SVM %d): Команда типа %u отправлена. Переход в состояние ожидания %d.\n", i, cmd_to_send_type, link->prep_state);
                } else {
                    fprintf(stderr, "UVM Main (SVM %d): Ошибка отправки команды подготовки типа %u. Установка FAILED.\n", i, cmd_to_send_type);
                    link->prep_state = PREP_STATE_FAILED;
                    link->status = UVM_LINK_FAILED;
                    snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop),
                             "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X,Reason=SendFailPrepCmd%u",
                             i, UVM_LINK_FAILED, link->assigned_lak, cmd_to_send_type);
                    send_to_gui_socket(gui_buffer_main_loop);
                }
                processed_in_iteration = true;
            }
        }
        pthread_mutex_unlock(&uvm_links_mutex);

        // === БЛОК B: ОБРАБОТКА ВХОДЯЩИХ ОТВЕТОВ ===
        if (uvq_dequeue(uvm_incoming_response_queue, &response_msg_data_main)) {
            processed_in_iteration = true;
            int svm_id_resp = response_msg_data_main.source_svm_id;
            Message *msg_resp = &response_msg_data_main.message;
            message_to_host_byte_order(msg_resp);
            uint16_t msg_num_resp = get_full_message_number(&msg_resp->header);

            pthread_mutex_lock(&uvm_links_mutex);
            if (svm_id_resp >= 0 && svm_id_resp < num_svms_in_config) {
                UvmSvmLink *link_resp = &svm_links[svm_id_resp];
                link_resp->last_activity_time = time(NULL);

                char gui_details_resp[256] = "N/A";
                char gui_bcb_field_resp[32] = "";
                bool bcb_present_resp = false;
                bool send_link_status_event_after_recv = false;
                UvmLinkStatus old_status_for_event = link_resp->status;

                // --- Логика извлечения деталей и BCB для GUI (должна быть здесь) ---
                switch(msg_resp->header.message_type) {
                    case MESSAGE_TYPE_CONFIRM_INIT:
                        if(msg_resp->header.body_length >= sizeof(ConfirmInitBody)) {
                            ConfirmInitBody *body = (ConfirmInitBody*)msg_resp->body;
                            link_resp->last_recv_bcb = body->bcb; // BCB уже в host order после message_to_host_byte_order
                            snprintf(gui_bcb_field_resp, sizeof(gui_bcb_field_resp), ";BCB:0x%08X", link_resp->last_recv_bcb);
                            bcb_present_resp = true;
                            snprintf(gui_details_resp, sizeof(gui_details_resp), "SLP=0x%02X;VDR=0x%02X;BOP1=0x%02X;BOP2=0x%02X",
                                     body->slp, body->vdr, body->bop1, body->bop2);
                        }
                        break;
                    case MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA:
                        if(msg_resp->header.body_length >= sizeof(PodtverzhdenieKontrolyaBody)) {
                            PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody*)msg_resp->body;
                            link_resp->last_recv_bcb = body->bcb;
                            snprintf(gui_bcb_field_resp, sizeof(gui_bcb_field_resp), ";BCB:0x%08X", link_resp->last_recv_bcb);
                            bcb_present_resp = true;
                            snprintf(gui_details_resp, sizeof(gui_details_resp), "TK=0x%02X", body->tk);
                        }
                        break;
                    case MESSAGE_TYPE_RESULTATY_KONTROLYA:
                        if(msg_resp->header.body_length >= sizeof(RezultatyKontrolyaBody)) {
                            RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody*)msg_resp->body;
                            link_resp->last_recv_bcb = body->bcb;
                            snprintf(gui_bcb_field_resp, sizeof(gui_bcb_field_resp), ";BCB:0x%08X", link_resp->last_recv_bcb);
                            bcb_present_resp = true;
                            snprintf(gui_details_resp, sizeof(gui_details_resp), "RSK=0x%02X;VSK=%ums", body->rsk, body->vsk);
                        }
                        break;
                    case MESSAGE_TYPE_SOSTOYANIE_LINII:
                        if(msg_resp->header.body_length >= sizeof(SostoyanieLiniiBody)) {
                            SostoyanieLiniiBody *body = (SostoyanieLiniiBody*)msg_resp->body;
                            link_resp->last_recv_bcb = body->bcb;
                            snprintf(gui_bcb_field_resp, sizeof(gui_bcb_field_resp), ";BCB:0x%08X", link_resp->last_recv_bcb);
                            bcb_present_resp = true;
                            snprintf(gui_details_resp, sizeof(gui_details_resp), "KLA=%u;SLA=%u;KSA=%u", body->kla, body->sla, body->ksa);
                        }
                        break;
                    case MESSAGE_TYPE_PREDUPREZHDENIE:
                         if(msg_resp->header.body_length >= sizeof(PreduprezhdenieBody)) {
                            PreduprezhdenieBody *body = (PreduprezhdenieBody*)msg_resp->body;
                            link_resp->last_recv_bcb = body->bcb;
                            snprintf(gui_bcb_field_resp, sizeof(gui_bcb_field_resp), ";BCB:0x%08X", link_resp->last_recv_bcb);
                            bcb_present_resp = true;
                            snprintf(gui_details_resp, sizeof(gui_details_resp), "TKS=%u", body->tks);
                        }
                        break;
                    default: // Для других типов сообщений (СУБК, КО и т.д., если они придут)
                        strcpy(gui_details_resp,"Async Data"); // Или более специфично
                        break;
                }
                // --- Отправка RECV в GUI ---
                snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop),
                         "RECV;SVM_ID:%d;Type:%d;Num:%u;LAK:0x%02X%s;Details:%s",
                         svm_id_resp, msg_resp->header.message_type, msg_num_resp,
                         msg_resp->header.address,
                         bcb_present_resp ? gui_bcb_field_resp : "",
                         gui_details_resp);
                send_to_gui_socket(gui_buffer_main_loop);

                // Логика переключения состояний подготовки
                if (link_resp->status == UVM_LINK_ACTIVE || link_resp->status == UVM_LINK_WARNING) { // Обрабатываем ответы только от "живых"
                    switch (link_resp->prep_state) {
                        case PREP_STATE_AWAITING_CONFIRM_INIT:
                            if (msg_resp->header.message_type == MESSAGE_TYPE_CONFIRM_INIT) {
                                ConfirmInitBody* ci_body_main = (ConfirmInitBody*)msg_resp->body; // BCB уже в host order
                                printf("UVM Main (SVM %d): Обработан ответ 'Подтверждение инициализации'.\n", svm_id_resp);
                                if (ci_body_main->lak == link_resp->assigned_lak) {
                                    link_resp->prep_state = PREP_STATE_AWAITING_CONFIRM_KONTROL; // Готов к отправке "Провести контроль" на след. итерации
                                    link_resp->current_preparation_msg_num++;
                                } else {
                                    fprintf(stderr, "UVM Main (SVM %d): LAK Mismatch! Expected 0x%02X, got 0x%02X\n", svm_id_resp, link_resp->assigned_lak, ci_body_main->lak);
                                    link_resp->prep_state = PREP_STATE_FAILED; link_resp->status = UVM_LINK_FAILED;
                                    link_resp->lak_mismatch_detected = true; send_link_status_event_after_recv = true;
                                    snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop), "EVENT;SVM_ID:%d;Type:LAKMismatch;Details:Expected=0x%02X,Got=0x%02X,Msg=ConfirmInit", svm_id_resp, link_resp->assigned_lak, ci_body_main->lak); send_to_gui_socket(gui_buffer_main_loop);
                                }
                            } // else если не тот тип - обработка ниже (асинхронные)
                            break;

                        case PREP_STATE_AWAITING_CONFIRM_KONTROL:
                            if (msg_resp->header.message_type == MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA) {
                                printf("UVM Main (SVM %d): Обработан ответ 'Подтверждение контроля'.\n", svm_id_resp);
                                link_resp->prep_state = PREP_STATE_AWAITING_RESULTS_KONTROL;
                                link_resp->current_preparation_msg_num++;
                            }
                            break;

                        case PREP_STATE_AWAITING_RESULTS_KONTROL:
                            if (msg_resp->header.message_type == MESSAGE_TYPE_RESULTATY_KONTROLYA) {
                                RezultatyKontrolyaBody* rk_body_main = (RezultatyKontrolyaBody*)msg_resp->body;
                                printf("UVM Main (SVM %d): Обработан ответ 'Результаты контроля'. RSK=0x%02X\n", svm_id_resp, rk_body_main->rsk);
                                if (rk_body_main->rsk != 0x3F) {
                                    link_resp->control_failure_detected = true; link_resp->last_control_rsk = rk_body_main->rsk;
                                    if(link_resp->status == UVM_LINK_ACTIVE) link_resp->status = UVM_LINK_WARNING;
                                    send_link_status_event_after_recv = true;
                                    snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop), "EVENT;SVM_ID:%d;Type:ControlFail;Details:RSK=0x%02X", svm_id_resp, rk_body_main->rsk); send_to_gui_socket(gui_buffer_main_loop);
                                } else {
                                    if(link_resp->control_failure_detected && link_resp->status == UVM_LINK_WARNING && !link_resp->lak_mismatch_detected && !link_resp->response_timeout_detected && link_resp->last_warning_tks == 0) {
                                        // Если был WARNING только из-за RSK, и теперь RSK ОК, и нет других проблем
                                        // link_resp->status = UVM_LINK_ACTIVE; // Осторожно с автоматическим возвратом в ACTIVE
                                        // send_link_status_event_after_recv = true;
                                    }
                                    link_resp->control_failure_detected = false;
                                }
                                link_resp->prep_state = PREP_STATE_AWAITING_LINE_STATUS;
                                link_resp->current_preparation_msg_num++;
                            }
                            break;

                        case PREP_STATE_AWAITING_LINE_STATUS:
                            if (msg_resp->header.message_type == MESSAGE_TYPE_SOSTOYANIE_LINII) {
                                printf("UVM Main (SVM %d): Обработан ответ 'Состояние линии'.\n", svm_id_resp);
                                link_resp->prep_state = PREP_STATE_PREPARATION_COMPLETE;
                                link_resp->current_preparation_msg_num++; // На всякий случай, хотя этот счетчик для команд подготовки
                                printf("UVM Main (SVM %d): Этап 'Подготовка к сеансу наблюдения' УСПЕШНО ЗАВЕРШЕН.\n", svm_id_resp);
                                send_link_status_event_after_recv = true; // Отправить текущий статус
                            }
                            break;
                        default: // PREP_STATE_NOT_STARTED, PREP_STATE_PREPARATION_COMPLETE, PREP_STATE_FAILED или асинхронное сообщение
                            if (link_resp->prep_state != PREP_STATE_PREPARATION_COMPLETE && link_resp->prep_state != PREP_STATE_FAILED) {
                                // Если пришло асинхронное сообщение (не то, что ожидали по prep_state)
                                if (msg_resp->header.message_type == MESSAGE_TYPE_PREDUPREZHDENIE) {
                                    PreduprezhdenieBody *warn_b = (PreduprezhdenieBody*)msg_resp->body;
                                    fprintf(stderr, "UVM Main (SVM %d): Асинхронно получено ПРЕДУПРЕЖДЕНИЕ TKS=%u в состоянии prep_state %d.\n", svm_id_resp, warn_b->tks, link_resp->prep_state);
                                    link_resp->last_warning_tks = warn_b->tks; link_resp->last_warning_time = time(NULL);
                                    if(link_resp->status == UVM_LINK_ACTIVE) link_resp->status = UVM_LINK_WARNING;
                                    send_link_status_event_after_recv = true;
                                    snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop), "EVENT;SVM_ID:%d;Type:Warning;Details:TKS=%u", svm_id_resp, warn_b->tks); send_to_gui_socket(gui_buffer_main_loop);
                                } else {
                                     fprintf(stderr, "UVM Main (SVM %d): Получен неожиданный тип %u (Num %u) в состоянии prep_state %d.\n",
                                           svm_id_resp, msg_resp->header.message_type, msg_num_resp, link_resp->prep_state);
                                }
                            } else if (link_resp->prep_state == PREP_STATE_PREPARATION_COMPLETE) {
                                // Сюда могут приходить СУБК, КО и т.д. Их нужно будет обрабатывать отдельно.
                                 printf("UVM Main (SVM %d): Получено сообщение тип %u (Num %u) после завершения подготовки.\n",
                                           svm_id_resp, msg_resp->header.message_type, msg_num_resp);
                            }
                            break;
                    } // end switch (link_resp->prep_state)

                    if (send_link_status_event_after_recv || (link_resp->status != old_status_for_event)) {
                         snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop),
                                 "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X",
                                 svm_id_resp, link_resp->status, link_resp->assigned_lak);
                        send_to_gui_socket(gui_buffer_main_loop);
                    }
                } // if link is active or warning
            } // if svm_id_resp valid
            pthread_mutex_unlock(&uvm_links_mutex);
        } // if uvq_dequeue

        // === БЛОК C: ПРОВЕРКА ТАЙМАУТОВ ОЖИДАНИЯ ОТВЕТОВ НА КОМАНДЫ ПОДГОТОВКИ ===
        pthread_mutex_lock(&uvm_links_mutex);
        time_t now_timeout_check = time(NULL);
        for (int k = 0; k < num_svms_in_config; ++k) {
            if (!config.svm_config_loaded[k]) continue;
            UvmSvmLink *link_to_check = &svm_links[k];
            time_t current_timeout_s = 0;
            const char* expected_cmd_name_for_timeout = "UnknownCmd";
            MessageType expected_reply_for_timeout = (MessageType)0;

            if (link_to_check->status == UVM_LINK_ACTIVE || link_to_check->status == UVM_LINK_WARNING) {
                switch (link_to_check->prep_state) {
                    case PREP_STATE_AWAITING_CONFIRM_INIT:
                        current_timeout_s = TIMEOUT_CONFIRM_INIT_S_MAIN;
                        expected_cmd_name_for_timeout = "InitChannel";
                        expected_reply_for_timeout = MESSAGE_TYPE_CONFIRM_INIT;
                        break;
                    case PREP_STATE_AWAITING_CONFIRM_KONTROL:
                        current_timeout_s = TIMEOUT_CONFIRM_KONTROL_S_MAIN;
                        expected_cmd_name_for_timeout = "ProvestiKontrol";
                        expected_reply_for_timeout = MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA;
                        break;
                    case PREP_STATE_AWAITING_RESULTS_KONTROL:
                        current_timeout_s = TIMEOUT_RESULTS_KONTROL_S_MAIN;
                        expected_cmd_name_for_timeout = "VydatRezultatyKontrolya";
                        expected_reply_for_timeout = MESSAGE_TYPE_RESULTATY_KONTROLYA;
                        break;
                    case PREP_STATE_AWAITING_LINE_STATUS:
                        current_timeout_s = TIMEOUT_LINE_STATUS_S_MAIN;
                        expected_cmd_name_for_timeout = "VydatSostoyanieLinii";
                        expected_reply_for_timeout = MESSAGE_TYPE_SOSTOYANIE_LINII;
                        break;
                    default: break;
                }

                if (current_timeout_s > 0 && link_to_check->last_command_sent_time > 0 &&
                    (now_timeout_check - link_to_check->last_command_sent_time) > current_timeout_s) {
                    
                    fprintf(stderr, "UVM Main (SVM %d): ТАЙМАУТ! Не получен ответ типа %d на команду '%s' (тип %u) в течение %ld сек.\n",
                           k, expected_reply_for_timeout, expected_cmd_name_for_timeout, link_to_check->last_sent_prep_cmd_type, current_timeout_s);
                    link_to_check->prep_state = PREP_STATE_FAILED;
                    link_to_check->status = UVM_LINK_FAILED;
                    link_to_check->response_timeout_detected = true;
                    
                    snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop),
                             "EVENT;SVM_ID:%d;Type:ResponseTimeout;Details:Cmd=%s,ExpectedReplyType=%d",
                             k, expected_cmd_name_for_timeout, expected_reply_for_timeout);
                    send_to_gui_socket(gui_buffer_main_loop);
                    
                    snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop),
                             "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X",
                             k, UVM_LINK_FAILED, link_to_check->assigned_lak);
                    send_to_gui_socket(gui_buffer_main_loop);
                    processed_in_iteration = true;
                }
            }
        }
        pthread_mutex_unlock(&uvm_links_mutex);
        
        // === БЛОК D: ПРОВЕРКА KEEP-ALIVE (остается как раньше) ===
        // (ваш существующий код проверки Keep-Alive, он должен работать параллельно
        //  и менять link->status на UVM_LINK_FAILED при необходимости)

        if (!processed_in_iteration && uvm_keep_running) {
            usleep(10000); // 10 мс, если ничего не произошло, чтобы не грузить CPU и дать шанс другим потокам
        }
    } // end while (uvm_keep_running && !all_svms_preparation_done_or_failed)

    if (uvm_keep_running && all_svms_preparation_done_or_failed) {
        printf("UVM: Все SVM либо завершили подготовку, либо отвалились.\n");
    }
	
	

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
    UvmResponseMessage response_msg_data;
    char gui_msg_buffer[512]; // Объявляем здесь, чтобы была доступна везде в цикле

    while (uvm_keep_running) {
        bool message_received_in_this_iteration = false;
        // Объявляем и инициализируем переменные для информации из сообщения
        // в начале каждой итерации главного цикла
        char details_for_gui[256] = "";
        char bcb_field_for_gui[32] = "";
        bool bcb_was_in_message = false;
        LogicalAddress expected_lak_for_log = 0; // Для логирования в консоль UVM
        // current_status будет получен из svm_links внутри блока if(uvq_dequeue)

        if (uvq_dequeue(uvm_incoming_response_queue, &response_msg_data)) {
            message_received_in_this_iteration = true;
            int svm_id = response_msg_data.source_svm_id;
            Message *msg = &response_msg_data.message;
            uint16_t msg_num = get_full_message_number(&msg->header);

            // --- Обновление данных в svm_links[svm_id] и подготовка данных для GUI ---
            pthread_mutex_lock(&uvm_links_mutex);
            if (svm_id >= 0 && svm_id < MAX_SVM_INSTANCES) { // Используем MAX_SVM_INSTANCES
                 UvmSvmLink *link = &svm_links[svm_id];
                 expected_lak_for_log = link->assigned_lak; // Для лога UVM
                 UvmLinkStatus current_link_status = link->status; // Локальная копия статуса

                 if(current_link_status == UVM_LINK_ACTIVE) {
                     link->last_activity_time = time(NULL);
                     link->last_recv_msg_type = msg->header.message_type;
                     link->last_recv_msg_num = msg_num;
                     link->last_recv_msg_time = time(NULL);

                     // Обработка тела для извлечения BCB и других деталей
                     switch(msg->header.message_type) {
                         case MESSAGE_TYPE_CONFIRM_INIT:
                             if(ntohs(msg->header.body_length) >= sizeof(ConfirmInitBody)) {
                                 ConfirmInitBody *body = (ConfirmInitBody*)msg->body;
                                 link->last_recv_bcb = ntohl(body->bcb);
                                 snprintf(bcb_field_for_gui, sizeof(bcb_field_for_gui), ";BCB:0x%08X", link->last_recv_bcb);
                                 bcb_was_in_message = true;
                                 if (body->lak != link->assigned_lak) {
                                     link->lak_mismatch_detected = true;
                                     link->status = UVM_LINK_FAILED;
                                     // Строки для EVENT будут сформированы и отправлены позже, вне мьютекса
                                 }
                             }
                             break;
                         case MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA:
                             if(ntohs(msg->header.body_length) >= sizeof(PodtverzhdenieKontrolyaBody)) {
                                 PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody*)msg->body;
                                 link->last_recv_bcb = ntohl(body->bcb);
                                 snprintf(bcb_field_for_gui, sizeof(bcb_field_for_gui), ";BCB:0x%08X", link->last_recv_bcb);
                                 bcb_was_in_message = true;
                                 snprintf(details_for_gui, sizeof(details_for_gui), "TK=0x%02X", body->tk);
                             }
                             break;
                         case MESSAGE_TYPE_RESULTATY_KONTROLYA:
                             if(ntohs(msg->header.body_length) >= sizeof(RezultatyKontrolyaBody)) {
                                 RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody*)msg->body;
                                 link->last_recv_bcb = ntohl(body->bcb);
                                 link->last_control_rsk = body->rsk;
                                 snprintf(bcb_field_for_gui, sizeof(bcb_field_for_gui), ";BCB:0x%08X", link->last_recv_bcb);
                                 bcb_was_in_message = true;
                                 snprintf(details_for_gui, sizeof(details_for_gui), "RSK=0x%02X;VSK=%ums", body->rsk, ntohs(body->vsk));
                                 if (body->rsk != 0x3F) {
                                     link->control_failure_detected = true;
                                     if(link->status == UVM_LINK_ACTIVE) link->status = UVM_LINK_WARNING;
                                 }
                             }
                             break;
						case MESSAGE_TYPE_SOSTOYANIE_LINII:
							if(ntohs(msg->header.body_length) >= sizeof(SostoyanieLiniiBody)) {
								SostoyanieLiniiBody *body = (SostoyanieLiniiBody*)msg->body;
								link->last_recv_bcb = ntohl(body->bcb);
								snprintf(bcb_field_for_gui, sizeof(bcb_field_for_gui), ";BCB:0x%08X", link->last_recv_bcb);
								bcb_was_in_message = true;
								uint16_t kla_host = ntohs(body->kla); // Локальные для snprintf
								uint32_t sla_host = ntohl(body->sla);
								uint16_t ksa_host = ntohs(body->ksa);
								snprintf(details_for_gui, sizeof(details_for_gui), "KLA=%u;SLA=%u;KSA=%u", kla_host, sla_host, ksa_host);
                             }
                             break;
                         case MESSAGE_TYPE_PREDUPREZHDENIE:
                             if(ntohs(msg->header.body_length) >= sizeof(PreduprezhdenieBody)) {
                                 PreduprezhdenieBody *body = (PreduprezhdenieBody*)msg->body;
                                 link->last_recv_bcb = ntohl(body->bcb);
                                 link->last_warning_tks = body->tks;
                                 link->last_warning_time = time(NULL);
                                 snprintf(bcb_field_for_gui, sizeof(bcb_field_for_gui), ";BCB:0x%08X", link->last_recv_bcb);
                                 bcb_was_in_message = true;
                                 snprintf(details_for_gui, sizeof(details_for_gui), "TKS=%u", body->tks);
                             }
                             break;
                         default:
                             break;
                     } // end switch msg type
                 } // end if current_link_status == ACTIVE
            } // end if svm_id valid
            pthread_mutex_unlock(&uvm_links_mutex);
            // --- Конец обновления svm_links ---

            // Проверяем статус линка ПОСЛЕ отпускания мьютекса (он мог измениться)
            // для принятия решения об обработке и логгировании
            pthread_mutex_lock(&uvm_links_mutex);
            UvmLinkStatus status_for_processing = UVM_LINK_INACTIVE;
            if (svm_id >= 0 && svm_id < MAX_SVM_INSTANCES) {
                 status_for_processing = svm_links[svm_id].status;
            }
            pthread_mutex_unlock(&uvm_links_mutex);


            if (status_for_processing != UVM_LINK_ACTIVE && status_for_processing != UVM_LINK_WARNING) {
                printf("UVM Main: Ignored message type %u from non-active/failed SVM ID %d (Status: %d)\n",
                       msg->header.message_type, svm_id, status_for_processing);
            } else {
                printf("UVM Main: Processing message type %u from SVM ID %d (Assigned LAK 0x%02X, Num %u)\n",
                       msg->header.message_type, svm_id, expected_lak_for_log, msg_num);

                snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
                         "RECV;SVM_ID:%d;Type:%d;Num:%u;LAK:0x%02X%s;Details:%s",
                         svm_id, msg->header.message_type, msg_num, msg->header.address,
                         bcb_was_in_message ? bcb_field_for_gui : "",
                         details_for_gui);
                send_to_gui_socket(gui_msg_buffer);

                // --- Отправка специфичных EVENT'ов в GUI (после основного RECV) ---
                pthread_mutex_lock(&uvm_links_mutex); // Снова берем мьютекс для чтения флагов
                if (svm_id >= 0 && svm_id < MAX_SVM_INSTANCES) {
                    UvmSvmLink *link = &svm_links[svm_id]; // Получаем актуальный link
                    if (link->lak_mismatch_detected) {
                         snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
                                  "EVENT;SVM_ID:%d;Type:LAKMismatch;Details:Expected=0x%02X,Got=0x%02X",
                                  svm_id, link->assigned_lak, ((ConfirmInitBody*)msg->body)->lak ); // LAK из тела ConfirmInit
                         send_to_gui_socket(gui_msg_buffer);
                         // Можно сбросить флаг после отправки, если событие одноразовое
                         // link->lak_mismatch_detected = false;
                         // Также отправляем обновление статуса, если он стал FAILED
                         snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
                                  "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X",
                                  svm_id, link->status, link->assigned_lak);
                         send_to_gui_socket(gui_msg_buffer);
                    }
                    if (link->control_failure_detected) {
                         snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
                                  "EVENT;SVM_ID:%d;Type:ControlFail;Details:RSK=0x%02X",
                                  svm_id, link->last_control_rsk);
                         send_to_gui_socket(gui_msg_buffer);
                         // link->control_failure_detected = false; // Сбросить?
                         snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
                                  "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X",
                                  svm_id, link->status, link->assigned_lak);
                         send_to_gui_socket(gui_msg_buffer);
                    }
                    if (link->last_warning_tks != 0 && (time(NULL) - link->last_warning_time < 2)) { // Отправляем только свежее предупреждение
                         snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
                                  "EVENT;SVM_ID:%d;Type:Warning;Details:TKS=%u",
                                  svm_id, link->last_warning_tks);
                         send_to_gui_socket(gui_msg_buffer);
                         // link->last_warning_tks = 0; // Сбросить?
                    }
                }
                pthread_mutex_unlock(&uvm_links_mutex);
                // --- Конец отправки EVENT'ов ---
            } // end if (status_for_processing == UVM_LINK_ACTIVE || WARNING)

        } else { // Очередь пуста или закрыта
            if (!uvm_keep_running) { break; }
        }

// --- Периодическая проверка Keep-Alive ---
        time_t now_keepalive = time(NULL);
        const time_t keepalive_timeout_sec = 15; // <-- УМЕНЬШИЛ ДЛЯ БЫСТРОГО ТЕСТА

        for (int k = 0; k < num_svms_in_config; ++k) {
             bool send_ka_event_flag = false; // Используем другое имя, чтобы не конфликтовать, если send_ka_event есть выше
             UvmLinkStatus current_link_status_for_ka = UVM_LINK_INACTIVE;
             LogicalAddress current_ka_lak_for_event = 0;
             bool timeout_triggered_now = false; // Флаг, что таймаут сработал именно на этой итерации

             pthread_mutex_lock(&uvm_links_mutex);
             if (k >= 0 && k < MAX_SVM_INSTANCES) { // Используем MAX_SVM_INSTANCES, как определено в config.h
                UvmSvmLink *link = &svm_links[k];
                current_link_status_for_ka = link->status; // Сохраняем для использования после unlock
                current_ka_lak_for_event = link->assigned_lak;

                // --- ДОБАВЛЕН ОТЛАДОЧНЫЙ PRINTF ---
                if (link->status == UVM_LINK_ACTIVE) { // Логируем только для активных, чтобы не засорять вывод
                    long time_diff = (link->last_activity_time == 0) ? -1 : (now_keepalive - link->last_activity_time);
                    printf("DEBUG KeepAlive Check SVM %d: Status=%d, LastActivity=%ld (Diff: %ld sec), TimeoutAfter=%ld sec\n",
                           k, link->status, link->last_activity_time, time_diff, keepalive_timeout_sec);
                }
                // --- КОНЕЦ ОТЛАДОЧНОГО PRINTF ---

                if (link->status == UVM_LINK_ACTIVE && link->last_activity_time != 0 &&
                    (now_keepalive - link->last_activity_time) > keepalive_timeout_sec)
                {
                     fprintf(stderr, "UVM Main: Keep-Alive TIMEOUT detected for SVM ID %d! (Last activity %ld s ago)\n",
                             k, (now_keepalive - link->last_activity_time));
                     link->status = UVM_LINK_FAILED;
                     link->timeout_detected = true; // Флаг, что таймаут был для этого линка
                     if (link->connection_handle >= 0) {
                          shutdown(link->connection_handle, SHUT_RDWR);
                     }
                     timeout_triggered_now = true; // Таймаут сработал именно сейчас
                }
             }
             pthread_mutex_unlock(&uvm_links_mutex);

             // Отправляем события, если таймаут сработал именно сейчас
             if(timeout_triggered_now){
                 snprintf(gui_msg_buffer, sizeof(gui_msg_buffer), "EVENT;SVM_ID:%d;Type:KeepAliveTimeout;Details:No activity for %ld sec", k, keepalive_timeout_sec);
                 send_to_gui_socket(gui_msg_buffer);
                 snprintf(gui_msg_buffer, sizeof(gui_msg_buffer), "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X", k, UVM_LINK_FAILED, current_ka_lak_for_event);
                 send_to_gui_socket(gui_msg_buffer);
                 // Флаг link->timeout_detected остается true, чтобы GUI мог это отображать,
                 // пока соединение не будет восстановлено (если будет реконнект) или UVM не завершится.
             }
        } // end for keep-alive check

        if (!message_received_in_this_iteration && uvm_keep_running) {
            usleep(100000); // 100 мс
        }
    } // end while (uvm_keep_running)

cleanup_connections:
    printf("UVM: Завершение работы и очистка ресурсов...\n");
    // Закрываем соединения и уничтожаем интерфейсы
    pthread_mutex_lock(&uvm_links_mutex);
	uvm_keep_running = false; // Устанавливаем флаг для всех потоков
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