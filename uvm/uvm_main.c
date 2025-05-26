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
    if (!uvm_outgoing_request_queue || !request) {
        fprintf(stderr, "send_uvm_request: ОШИБКА: очередь или запрос NULL.\n");
        return false;
    }
    // bool success = true; // success будет определяться по результату enqueue
    char gui_msg_buffer[300];

    printf("send_uvm_request: Вход. Запрос для SVM %d, тип протокольного сообщения %d, тип UVM запроса %d.\n",
           request->target_svm_id, request->message.header.message_type, request->type);

    if (request->type == UVM_REQ_SEND_MESSAGE) {
        //printf("send_uvm_request: Тип UVM запроса UVM_REQ_SEND_MESSAGE. Проверка линка (мьютекс должен быть уже взят вызывающим!)...\n"); // ИЗМЕНЕН КОММЕНТАРИЙ

        if (request->target_svm_id >= 0 && request->target_svm_id < MAX_SVM_INSTANCES) {
            UvmSvmLink *link = &svm_links[request->target_svm_id]; // ДОСТУП К svm_links ТЕПЕРЬ НЕ ЗАЩИЩЕН ЗДЕСЬ!
            //printf("send_uvm_request: Статус линка для SVM %d: %d (Ожидаем UVM_LINK_ACTIVE = %d).\n", request->target_svm_id, link->status, UVM_LINK_ACTIVE);

            if (link->status == UVM_LINK_ACTIVE) { // Проверяем статус ПОСЛЕ получения указателя на link
                link->last_sent_msg_type = request->message.header.message_type;
                link->last_sent_msg_num = get_full_message_number(&request->message.header);
                link->last_sent_msg_time = time(NULL);

                // --- Вычисление веса для SENT ---
                // Предполагаем, что request->message.header.body_length УЖЕ в хостовом порядке на этом этапе,
                // так как оно заполняется билдерами, которые работают с хостовым порядком,
                // а в сетевой преобразуется только в io_common.c перед самой отправкой.
                // Если это не так, и body_length здесь в сетевом, нужно будет сделать ntohs() перед вычислением.
                // Давайте для безопасности сделаем копию и преобразуем ее, если нужно.
                Message temp_msg_for_weight = request->message; // Копируем структуру
                message_to_host_byte_order(&temp_msg_for_weight); // Гарантируем хостовый порядок для body_length
                uint16_t body_len_sent_host = temp_msg_for_weight.header.body_length;
                size_t weight_sent = sizeof(MessageHeader) + body_len_sent_host;
				// printf("DEBUG SENT Type 160: body_len_sent_host = %u, sizeof(PrinyatParametrySoBody) = %zu, calculated_weight = %zu\n", body_len_sent_host, sizeof(PrinyatParametrySoBody), weight_sent);

                // --- КОНЕЦ---


                // Отправка в GUI должна быть безопасной относительно gui_socket_mutex,
                snprintf(gui_msg_buffer, sizeof(gui_msg_buffer),
                         "SENT;SVM_ID:%d;Type:%d;Num:%u;LAK:0x%02X;Weight:%zu", // <-- ДОБАВЛЕНО ;Weight:%zu
                         request->target_svm_id,
                         request->message.header.message_type,
                         link->last_sent_msg_num,
                         link->assigned_lak,
                         weight_sent); // <-- ПЕРЕДАЕМ ВЕС
                send_to_gui_socket(gui_msg_buffer); // send_to_gui_socket сама берет свой мьютекс
                //printf("send_uvm_request: SENT событие отправлено в GUI для SVM %d, тип %d.\n", request->target_svm_id, request->message.header.message_type);
            } else {
                printf("send_uvm_request: Линк для SVM %d НЕ АКТИВЕН (статус %d), SENT в GUI не отправлен. Команда НЕ будет поставлена в очередь.\n", request->target_svm_id, link->status);
                // pthread_mutex_unlock(&uvm_links_mutex); // <--- УБРАТЬ (если бы она была здесь)
                // Если линк не активен, мы не должны ставить команду в очередь и не должны увеличивать uvm_outstanding_sends
                return false; // <--- ВАЖНО: возвращаем false, чтобы main знал, что отправка не удалась
            }
        } else {
            fprintf(stderr, "send_uvm_request: ОШИБКА: невалидный target_svm_id %d.\n", request->target_svm_id);
            // pthread_mutex_unlock(&uvm_links_mutex); // <--- УБРАТЬ (если бы она была здесь)
            return false; // Ошибка, не ставим в очередь
        }

        // Увеличиваем счетчик только если собираемся ставить в очередь (т.е. если дошли сюда и не было return false)
        pthread_mutex_lock(&uvm_send_counter_mutex);
        uvm_outstanding_sends++;
        pthread_mutex_unlock(&uvm_send_counter_mutex);
        //printf("send_uvm_request: uvm_outstanding_sends увеличен до %d.\n", uvm_outstanding_sends);
    } else {
        fprintf(stderr, "send_uvm_request: ВНИМАНИЕ: request->type (%d) НЕ UVM_REQ_SEND_MESSAGE. Пропуск обновления GUI и счетчика sends.\n", request->type);
    }

    // Постановка в очередь
    if (!queue_req_enqueue(uvm_outgoing_request_queue, request)) {
        fprintf(stderr, "send_uvm_request: ОШИБКА: Failed to enqueue request (prot.msg type %d, target_svm_id %d)\n",
                request->message.header.message_type, request->target_svm_id);
        if (request->type == UVM_REQ_SEND_MESSAGE) { // Только если мы его увеличивали
             pthread_mutex_lock(&uvm_send_counter_mutex);
             if(uvm_outstanding_sends > 0) uvm_outstanding_sends--;
             //printf("send_uvm_request: uvm_outstanding_sends уменьшен (ошибка enqueue) до %d.\n", uvm_outstanding_sends);
             pthread_mutex_unlock(&uvm_send_counter_mutex);
        }
        return false;
    } else {
        //printf("send_uvm_request: Запрос для SVM %d, тип протокольного сообщения %d УСПЕШНО помещен в очередь.\n", request->target_svm_id, request->message.header.message_type);
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

    // --- Новый основной цикл управления SVM с машиной состояний ---
    printf("UVM: Начало основного цикла управления SVM (Асинхронная подготовка)...\n");
    UvmResponseMessage response_msg_data_main; // Для чтения из очереди ответов
    char gui_buffer_main_loop[512];          // Буфер для сообщений в GUI

    // Таймауты для разных ответов (в секундах)
    const time_t TIMEOUT_CONFIRM_INIT_S_MAIN = 5;
    const time_t TIMEOUT_CONFIRM_KONTROL_S_MAIN = 12; // Учитывая возможную задержку SVM при самоконтроле
    const time_t TIMEOUT_RESULTS_KONTROL_S_MAIN = 8;  // SVM может "думать" до 6с + передача
    const time_t TIMEOUT_LINE_STATUS_S_MAIN = 5;

    // Инициализируем начальное состояние подготовки для всех активных TCP-линков
    pthread_mutex_lock(&uvm_links_mutex);
    for (int i = 0; i < num_svms_in_config; ++i) {
        if (config.svm_config_loaded[i] && svm_links[i].status == UVM_LINK_ACTIVE) {
            svm_links[i].prep_state = PREP_STATE_READY_TO_SEND_INIT_CHANNEL; // Начальное состояние для отправки InitChannel
            svm_links[i].current_preparation_msg_num = 0;     // Сброс счетчика сообщений подготовки
            svm_links[i].last_command_sent_time = 0;          // Сброс времени последней команды
        } else if (config.svm_config_loaded[i]) { // Если сконфигурирован, но TCP не активен
            svm_links[i].prep_state = PREP_STATE_FAILED;      // Сразу в ошибку подготовки
        }
    }
    pthread_mutex_unlock(&uvm_links_mutex);


    while (uvm_keep_running) {
        bool processed_something_this_iteration = false; // Флаг, что на этой итерации что-то сделали

        pthread_mutex_lock(&uvm_links_mutex);
        for (int i = 0; i < num_svms_in_config; ++i) {
            if (!config.svm_config_loaded[i]) continue;
            UvmSvmLink *link = &svm_links[i];
            bool command_to_send_prepared_now = false;
            UvmRequest request_to_send;
            request_to_send.type = UVM_REQ_SEND_MESSAGE;
            request_to_send.target_svm_id = i;
            // MessageType temp_sent_cmd_type = (MessageType)0; // Используем link->last_sent_prep_cmd_type

            if (link->status == UVM_LINK_ACTIVE || link->status == UVM_LINK_WARNING) { // Работаем также если статус WARNING (например, после ControlFail)
                
                switch (link->prep_state) {
                    // --- Кейсы для этапа "Подготовка к сеансу наблюдения" (как были) ---
                    case PREP_STATE_READY_TO_SEND_INIT_CHANNEL:
                        request_to_send.message = create_init_channel_message(LOGICAL_ADDRESS_UVM_VAL, link->assigned_lak, link->current_preparation_msg_num);
                        InitChannelBody *init_b_s1 = (InitChannelBody*)request_to_send.message.body; init_b_s1->lauvm = LOGICAL_ADDRESS_UVM_VAL; init_b_s1->lak = link->assigned_lak;
                        link->last_sent_prep_cmd_type = MESSAGE_TYPE_INIT_CHANNEL; // Сохраняем тип отправляемой команды
                        command_to_send_prepared_now = true;
                        // printf("UVM Main (SVM %d): Команда 'Инициализация канала' (Num %u) ПОДГОТОВЛЕНА к отправке.\n", i, link->current_preparation_msg_num);
                        break;
                    case PREP_STATE_READY_TO_SEND_PROVESTI_KONTROL:
                        request_to_send.message = create_provesti_kontrol_message(link->assigned_lak, 0x01, link->current_preparation_msg_num);
                        ProvestiKontrolBody* pk_b_s1 = (ProvestiKontrolBody*)request_to_send.message.body; pk_b_s1->tk = 0x01; request_to_send.message.header.body_length = htons(sizeof(ProvestiKontrolBody));
                        link->last_sent_prep_cmd_type = MESSAGE_TYPE_PROVESTI_KONTROL;
                        command_to_send_prepared_now = true;
                        break;
                    case PREP_STATE_READY_TO_SEND_VYDAT_REZ:
                        request_to_send.message = create_vydat_rezultaty_kontrolya_message(link->assigned_lak, 0x0F, link->current_preparation_msg_num);
                        VydatRezultatyKontrolyaBody* vrk_b_s1 = (VydatRezultatyKontrolyaBody*)request_to_send.message.body; vrk_b_s1->vrk = 0x0F; request_to_send.message.header.body_length = htons(sizeof(VydatRezultatyKontrolyaBody));
                        link->last_sent_prep_cmd_type = MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA;
                        command_to_send_prepared_now = true;
                        break;
                    case PREP_STATE_READY_TO_SEND_VYDAT_SOST:
                        request_to_send.message = create_vydat_sostoyanie_linii_message(link->assigned_lak, link->current_preparation_msg_num);
                        link->last_sent_prep_cmd_type = MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII;
                        command_to_send_prepared_now = true;
                        break;

                    // --- НОВЫЙ КЕЙС: ПОДГОТОВКА ЗАВЕРШЕНА, ОТПРАВЛЯЕМ ПАРАМЕТРЫ СЪЕМКИ ---
                    case PREP_STATE_PREPARATION_COMPLETE:
                        printf("UVM Main (SVM %d): Подготовка завершена. Отправка параметров съемки (Режим: %d, LAK: 0x%02X)...\n",
                               i, mode, link->assigned_lak);
                        
                        // Отправляем "пачку" команд подготовки к съемке.
                        // Они не требуют ответа, поэтому отправляем все сразу.
                        // Используем link->current_preparation_msg_num как стартовый номер для этой пачки.
                        uint16_t shoot_params_msg_num_start = link->current_preparation_msg_num;

                        if (mode == MODE_DR) {
                             request_to_send.message = create_prinyat_parametry_sdr_message(link->assigned_lak, shoot_params_msg_num_start++);
                             PrinyatParametrySdrBodyBase sdr_b_f1 = {0}; sdr_b_f1.pp_nl=(uint8_t)mode|(i & 0x03); /* Убедитесь, что i здесь корректно для номера луча */ memcpy(request_to_send.message.body, &sdr_b_f1, sizeof(sdr_b_f1));
                             request_to_send.message.header.body_length = htons(sizeof(sdr_b_f1)); send_uvm_request(&request_to_send);

                             request_to_send.message = create_prinyat_parametry_tsd_message(link->assigned_lak, shoot_params_msg_num_start++);
                             PrinyatParametryTsdBodyBase tsd_b_f1 = {0}; memcpy(request_to_send.message.body, &tsd_b_f1, sizeof(tsd_b_f1));
                             request_to_send.message.header.body_length = htons(sizeof(tsd_b_f1)); send_uvm_request(&request_to_send);
                        } else if (mode == MODE_OR || mode == MODE_OR1) {
                             request_to_send.message = create_prinyat_parametry_so_message(link->assigned_lak, shoot_params_msg_num_start++);
                             PrinyatParametrySoBody so_b_f1 = {0}; so_b_f1.pp=mode; memcpy(request_to_send.message.body, &so_b_f1, sizeof(so_b_f1));
                             request_to_send.message.header.body_length = htons(sizeof(so_b_f1)); send_uvm_request(&request_to_send);

                             request_to_send.message = create_prinyat_parametry_3tso_message(link->assigned_lak, shoot_params_msg_num_start++);
                             PrinyatParametry3TsoBody tso_b_f1 = {0}; memcpy(request_to_send.message.body, &tso_b_f1, sizeof(tso_b_f1));
                             request_to_send.message.header.body_length = htons(sizeof(tso_b_f1)); send_uvm_request(&request_to_send);
                            
                             request_to_send.message = create_prinyat_time_ref_range_message(link->assigned_lak, shoot_params_msg_num_start++);
                             PrinyatTimeRefRangeBody trr_b_f1 = {0}; memcpy(request_to_send.message.body, &trr_b_f1, sizeof(trr_b_f1));
                             request_to_send.message.header.body_length = htons(sizeof(trr_b_f1)); send_uvm_request(&request_to_send);

                             request_to_send.message = create_prinyat_reper_message(link->assigned_lak, shoot_params_msg_num_start++);
                             PrinyatReperBody rep_b_f1 = {0}; memcpy(request_to_send.message.body, &rep_b_f1, sizeof(rep_b_f1));
                             request_to_send.message.header.body_length = htons(sizeof(rep_b_f1)); send_uvm_request(&request_to_send);
                        } else if (mode == MODE_VR) {
                             request_to_send.message = create_prinyat_parametry_so_message(link->assigned_lak, shoot_params_msg_num_start++);
                             PrinyatParametrySoBody so_b_f_vr1 = {0}; so_b_f_vr1.pp=mode; memcpy(request_to_send.message.body, &so_b_f_vr1, sizeof(so_b_f_vr1));
                             request_to_send.message.header.body_length = htons(sizeof(so_b_f_vr1)); send_uvm_request(&request_to_send);

                             request_to_send.message = create_prinyat_parametry_3tso_message(link->assigned_lak, shoot_params_msg_num_start++);
                             PrinyatParametry3TsoBody tso_b_f_vr1 = {0}; memcpy(request_to_send.message.body, &tso_b_f_vr1, sizeof(tso_b_f_vr1));
                             request_to_send.message.header.body_length = htons(sizeof(tso_b_f_vr1)); send_uvm_request(&request_to_send);
                        }
                        // Навигационные данные для всех режимов
                        request_to_send.message = create_navigatsionnye_dannye_message(link->assigned_lak, shoot_params_msg_num_start++);
                        NavigatsionnyeDannyeBody nav_b_f1 = {0}; memcpy(request_to_send.message.body, &nav_b_f1, sizeof(nav_b_f1)); // Заполните тело nav_b_f1, если нужно
                        request_to_send.message.header.body_length = htons(sizeof(nav_b_f1)); 
                        if(send_uvm_request(&request_to_send)) { // Проверяем результат последней отправки
                           link->current_preparation_msg_num = shoot_params_msg_num_start; // Обновляем счетчик на следующий свободный
                           // Переводим в новое состояние, например, "Ожидание начала съемки" или "Параметры съемки отправлены"
                           // Пока просто оставим PREPARATION_COMPLETE, чтобы этот блок не срабатывал повторно.
                           // TODO: Ввести новое состояние SHOOTING_PARAMS_SENT или аналогичное.
						   link->prep_state = PREP_STATE_SHOOTING_PARAMS_SENT; // <--- ИЗМЕНЕНИЕ ЗДЕСЬ
                           printf("UVM Main (SVM %d): Параметры съемки отправлены. Состояние остается PREPARATION_COMPLETE (пока).\n", i);
                        } else {
                            // Ошибка отправки последнего сообщения из пачки
                             link->prep_state = PREP_STATE_FAILED; link->status = UVM_LINK_FAILED;
                        }
                        command_to_send_prepared_now = false; // Мы уже отправили все, что нужно в этом case
                        processed_something_this_iteration = true;
                        break;

                    default: // В состояниях AWAITING_..._REPLY или FAILED ничего не отправляем из этого блока
                        break;
                }

                // Этот блок if теперь только для команд ПОДГОТОВКИ К НАБЛЮДЕНИЮ
                if (command_to_send_prepared_now && link->prep_state != PREP_STATE_PREPARATION_COMPLETE) {
                    printf("UVM Main (SVM %d): Отправка команды подготовки типа %u (Num %u)...\n", i, link->last_sent_prep_cmd_type, link->current_preparation_msg_num);
                    if (send_uvm_request(&request_to_send)) {
                        link->last_command_sent_time = time(NULL);
                        // Переводим в соответствующее состояние ОЖИДАНИЯ ОТВЕТА
                        if (link->last_sent_prep_cmd_type == MESSAGE_TYPE_INIT_CHANNEL) {
                            link->prep_state = PREP_STATE_AWAITING_CONFIRM_INIT_REPLY;
                        } else if (link->last_sent_prep_cmd_type == MESSAGE_TYPE_PROVESTI_KONTROL) {
                            link->prep_state = PREP_STATE_AWAITING_PODTV_KONTROL_REPLY;
                        } else if (link->last_sent_prep_cmd_type == MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA) {
                            link->prep_state = PREP_STATE_AWAITING_REZ_KONTROL_REPLY;
                        } else if (link->last_sent_prep_cmd_type == MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII) {
                            link->prep_state = PREP_STATE_AWAITING_LINE_STATUS_REPLY;
                        }
                        printf("UVM Main (SVM %d): Команда подготовки типа %u (Num %u) отправлена. Переход в состояние ожидания %d.\n",
                               i, link->last_sent_prep_cmd_type, link->current_preparation_msg_num, link->prep_state);
                    } else { /* ... обработка ошибки send_uvm_request для команд подготовки ... */ }
                    processed_something_this_iteration = true;
                }
            } // if link active or warning
        } // for each svm
        pthread_mutex_unlock(&uvm_links_mutex);


// === БЛОК 2: ОБРАБОТКА ВХОДЯЩИХ ОТВЕТОВ ===
        if (uvq_dequeue(uvm_incoming_response_queue, &response_msg_data_main)) {
            processed_something_this_iteration = true; // Пометили, что что-то обработали
			bool is_expected_reply = false; // <--- ОБЪЯВИТЕ ЗДЕСЬ
			bool reply_is_ok_for_state_change = true;
            int svm_id_resp = response_msg_data_main.source_svm_id;
            Message *msg_resp = &response_msg_data_main.message;
            
            uint16_t msg_num_resp = get_full_message_number(&msg_resp->header);

            // Блокируем доступ к общему списку линков
            pthread_mutex_lock(&uvm_links_mutex);
            if (svm_id_resp >= 0 && svm_id_resp < num_svms_in_config) { // Проверяем валидность ID
                UvmSvmLink *link_resp = &svm_links[svm_id_resp];
                link_resp->last_activity_time = time(NULL); // Обновляем время последней активности

				// --- Вычисление веса для RECV ---
                // ТЕПЕРЬ msg_resp->header.body_length должно быть правильным (хостовым)
                uint16_t body_len_recv_host_for_gui = msg_resp->header.body_length; 
                size_t weight_recv_for_gui = sizeof(MessageHeader) + body_len_recv_host_for_gui;
                printf("DEBUG UVM_MAIN RECV: svm_id=%d, msg_type=%u, body_len_host=%u, calculated_weight=%zu\n", 
                       svm_id_resp, msg_resp->header.message_type, body_len_recv_host_for_gui, weight_recv_for_gui);

                // Переменные для формирования сообщения в GUI
                char gui_details_for_recv[256] = "N/A";
                char gui_bcb_for_recv[32] = "";
                bool bcb_found_for_recv = false;
                UvmLinkStatus status_before_processing = link_resp->status; // Сохраняем для сравнения
                bool send_specific_event_to_gui = false; // Флаг для отправки специфичных EVENT (LAKMismatch, ControlFail)
                char specific_event_buffer[256];       // Буфер для этих специфичных EVENT

                // --- Извлечение деталей из ТЕЛА сообщения для GUI и для логики UVM ---
                // Эта часть нужна, чтобы корректно отобразить детали в RECV и использовать их в switch ниже
                switch(msg_resp->header.message_type) {
                    case MESSAGE_TYPE_CONFIRM_INIT:
                        if (msg_resp->header.body_length >= sizeof(ConfirmInitBody)) {
                            ConfirmInitBody* cib_d = (ConfirmInitBody*)msg_resp->body;
                            snprintf(gui_bcb_for_recv, sizeof(gui_bcb_for_recv), ";BCB:0x%08X", cib_d->bcb); bcb_found_for_recv = true;
                            snprintf(gui_details_for_recv, sizeof(gui_details_for_recv), "SLP=0x%02X;VDR=0x%02X;BOP1=0x%02X;BOP2=0x%02X", cib_d->slp, cib_d->vdr, cib_d->bop1, cib_d->bop2);
                            link_resp->last_recv_bcb = cib_d->bcb;
                        }
                        break;
                    case MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA:
                        if (msg_resp->header.body_length >= sizeof(PodtverzhdenieKontrolyaBody)) {
                            PodtverzhdenieKontrolyaBody* pkb_d = (PodtverzhdenieKontrolyaBody*)msg_resp->body;
                            snprintf(gui_bcb_for_recv, sizeof(gui_bcb_for_recv), ";BCB:0x%08X", pkb_d->bcb); bcb_found_for_recv = true;
                            snprintf(gui_details_for_recv, sizeof(gui_details_for_recv), "TK=0x%02X", pkb_d->tk);
                            link_resp->last_recv_bcb = pkb_d->bcb;
                        }
                        break;
                    case MESSAGE_TYPE_RESULTATY_KONTROLYA:
                        if (msg_resp->header.body_length >= sizeof(RezultatyKontrolyaBody)) {
                            RezultatyKontrolyaBody* rkb_d = (RezultatyKontrolyaBody*)msg_resp->body;
                            snprintf(gui_bcb_for_recv, sizeof(gui_bcb_for_recv), ";BCB:0x%08X", rkb_d->bcb); bcb_found_for_recv = true;
                            snprintf(gui_details_for_recv, sizeof(gui_details_for_recv), "RSK=0x%02X;VSK=%ums", rkb_d->rsk, rkb_d->vsk);
                            link_resp->last_recv_bcb = rkb_d->bcb;
                            link_resp->last_control_rsk = rkb_d->rsk; // Сохраняем RSK
                        }
                        break;
                    case MESSAGE_TYPE_SOSTOYANIE_LINII:
                        if (msg_resp->header.body_length >= sizeof(SostoyanieLiniiBody)) {
                            SostoyanieLiniiBody* slb_d = (SostoyanieLiniiBody*)msg_resp->body;
                            snprintf(gui_bcb_for_recv, sizeof(gui_bcb_for_recv), ";BCB:0x%08X", slb_d->bcb); bcb_found_for_recv = true;
                            snprintf(gui_details_for_recv, sizeof(gui_details_for_recv), "KLA=%u;SLA=%u;KSA=%u", slb_d->kla, slb_d->sla, slb_d->ksa);
                            link_resp->last_recv_bcb = slb_d->bcb;
                        }
                        break;
                    case MESSAGE_TYPE_PREDUPREZHDENIE:
                        if (msg_resp->header.body_length >= sizeof(PreduprezhdenieBody)) {
                            PreduprezhdenieBody* warn_b_d = (PreduprezhdenieBody*)msg_resp->body;
                            snprintf(gui_bcb_for_recv, sizeof(gui_bcb_for_recv), ";BCB:0x%08X", warn_b_d->bcb); bcb_found_for_recv = true;
                            snprintf(gui_details_for_recv, sizeof(gui_details_for_recv), "TKS=%u", warn_b_d->tks);
                            link_resp->last_recv_bcb = warn_b_d->bcb;
                        }
                        break;
                    default: // Для других типов (СУБК, КО и т.д.)
                        strcpy(gui_details_for_recv, "Data/Unknown");
                        break;
                }

                // --- Отправка RECV сообщения в GUI ---
				snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop),
						 "RECV;SVM_ID:%d;Type:%d;Num:%u;LAK:0x%02X%s;Weight:%zu;Details:%s", // <-- ДОБАВЛЕНО ;Weight:%zu
						 svm_id_resp, msg_resp->header.message_type, msg_num_resp,
						 msg_resp->header.address,
						 bcb_found_for_recv ? gui_bcb_for_recv : "",
						 weight_recv_for_gui, // <-- ПЕРЕДАЕМ ВЕС
						 gui_details_for_recv);
				send_to_gui_socket(gui_buffer_main_loop);


                // --- Логика машины состояний подготовки ---
                bool reply_ok_for_state_transition = true; // По умолчанию считаем ответ достаточным для перехода

                // Обрабатываем только если линк еще не в FAILED или COMPLETE на этапе подготовки
                if (link_resp->prep_state != PREP_STATE_FAILED && link_resp->prep_state != PREP_STATE_PREPARATION_COMPLETE) {
                    switch (link_resp->prep_state) {
                        case PREP_STATE_AWAITING_CONFIRM_INIT_REPLY:
                            if (msg_resp->header.message_type == MESSAGE_TYPE_CONFIRM_INIT) {
								is_expected_reply = true;
                                ConfirmInitBody* ci_body_recv = (ConfirmInitBody*)msg_resp->body;
                                printf("UVM Main (SVM %d): Обработка ответа 'Подтверждение инициализации'.\n", svm_id_resp);
                                if (ci_body_recv->lak != link_resp->assigned_lak) {
                                    fprintf(stderr, "UVM Main (SVM %d): LAK Mismatch в ConfirmInit! Expected 0x%02X, got 0x%02X\n", svm_id_resp, link_resp->assigned_lak, ci_body_recv->lak);
                                    reply_ok_for_state_transition = false;
                                    link_resp->lak_mismatch_detected = true;
                                    snprintf(specific_event_buffer, sizeof(specific_event_buffer), "EVENT;SVM_ID:%d;Type:LAKMismatch;Details:Expected=0x%02X,Got=0x%02X,Msg=ConfirmInit", svm_id_resp, link_resp->assigned_lak, ci_body_recv->lak);
                                    send_specific_event_to_gui = true;
                                }
                                if (reply_ok_for_state_transition) {
                                    link_resp->prep_state = PREP_STATE_READY_TO_SEND_PROVESTI_KONTROL;
                                    link_resp->current_preparation_msg_num++;
                                }
                            } // else если тип не тот - будет обработано ниже как "неожиданный"
                            break;

                        case PREP_STATE_AWAITING_PODTV_KONTROL_REPLY:
                            if (msg_resp->header.message_type == MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA) {
								is_expected_reply = true;
                                // PodtverzhdenieKontrolyaBody* pkb_recv = (PodtverzhdenieKontrolyaBody*)msg_resp->body;
                                printf("UVM Main (SVM %d): Обработка ответа 'Подтверждение контроля'.\n", svm_id_resp);
                                // (Доп. проверки, если нужны, например, LAK в теле pkb_recv->lak == link_resp->assigned_lak)
                                link_resp->prep_state = PREP_STATE_READY_TO_SEND_VYDAT_REZ;
                                link_resp->current_preparation_msg_num++;
                            }
                            break;

                        case PREP_STATE_AWAITING_REZ_KONTROL_REPLY:
                            if (msg_resp->header.message_type == MESSAGE_TYPE_RESULTATY_KONTROLYA) {
								is_expected_reply = true;
                                RezultatyKontrolyaBody* rkb_recv = (RezultatyKontrolyaBody*)msg_resp->body;
                                printf("UVM Main (SVM %d): Обработка ответа 'Результаты контроля'. RSK=0x%02X\n", svm_id_resp, rkb_recv->rsk);
                                if (rkb_recv->rsk != 0x3F) { // 0x3F - код "ОК"
                                    // Не ставим reply_ok_for_state_transition = false, так как ответ все же пришел.
                                    // Но фиксируем ошибку контроля и меняем статус UVM_LINK на WARNING.
                                    link_resp->control_failure_detected = true;
                                    // link_resp->last_control_rsk уже установлен при извлечении деталей
                                    if(link_resp->status == UVM_LINK_ACTIVE) link_resp->status = UVM_LINK_WARNING;
                                    snprintf(specific_event_buffer, sizeof(specific_event_buffer), "EVENT;SVM_ID:%d;Type:ControlFail;Details:RSK=0x%02X", svm_id_resp, rkb_recv->rsk);
                                    send_specific_event_to_gui = true;
                                } else {
                                    if(link_resp->control_failure_detected) link_resp->control_failure_detected = false;
                                }
                                link_resp->prep_state = PREP_STATE_READY_TO_SEND_VYDAT_SOST;
                                link_resp->current_preparation_msg_num++;
                            }
                            break;

                        case PREP_STATE_AWAITING_LINE_STATUS_REPLY:
                            if (msg_resp->header.message_type == MESSAGE_TYPE_SOSTOYANIE_LINII) {
								is_expected_reply = true;
                                // SostoyanieLiniiBody* slb_recv = (SostoyanieLiniiBody*)msg_resp->body;
                                printf("UVM Main (SVM %d): Обработка ответа 'Состояние линии'.\n", svm_id_resp);
                                link_resp->prep_state = PREP_STATE_PREPARATION_COMPLETE;
                                link_resp->current_preparation_msg_num++; // Этот номер уже для команд этапа съемки
                                printf("UVM Main (SVM %d): Этап 'Подготовка к сеансу наблюдения' ЗАВЕРШЕН (prep_state=%d).\n", svm_id_resp, link_resp->prep_state);
                            }
                            break;
                        default:
                            // Если мы не в состоянии ожидания ответа подготовки, значит это асинхронное сообщение
                            // или сообщение этапа съемки (если он уже начался для этого SVM)
                            is_expected_reply = false; // Помечаем, что это не ожидаемый ответ на команду подготовки
                            break;
                    } // конец switch (link_resp->prep_state)

                    if (is_expected_reply && !reply_ok_for_state_transition) { // Если ждали ответ подготовки, но он был плохой
                        link_resp->prep_state = PREP_STATE_FAILED;
                        link_resp->status = UVM_LINK_FAILED;
                    }
                } // if prep_state not FAILED or COMPLETE

                // Обработка "Предупреждения" как асинхронного сообщения (может прийти в любом состоянии)
                if (msg_resp->header.message_type == MESSAGE_TYPE_PREDUPREZHDENIE) {
                    PreduprezhdenieBody *warn_b_main_recv = (PreduprezhdenieBody*)msg_resp->body;
                    fprintf(stderr, "UVM Main (SVM %d): Получено ПРЕДУПРЕЖДЕНИЕ TKS=%u.\n", svm_id_resp, warn_b_main_recv->tks);
                    link_resp->last_warning_tks = warn_b_main_recv->tks;
                    link_resp->last_warning_time = time(NULL);
                    if(link_resp->status == UVM_LINK_ACTIVE) link_resp->status = UVM_LINK_WARNING;
                    snprintf(specific_event_buffer, sizeof(specific_event_buffer), "EVENT;SVM_ID:%d;Type:Warning;Details:TKS=%u", svm_id_resp, warn_b_main_recv->tks);
                    send_specific_event_to_gui = true; // Ставим флаг, чтобы отправить после RECV
                }
                
                // Отправляем специфичный EVENT, если был установлен флаг
                if (send_specific_event_to_gui) {
                    send_to_gui_socket(specific_event_buffer);
                }

                // Отправляем событие об изменении общего статуса линка, если он изменился
                if (link_resp->status != status_before_processing) {
                     snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop),
                             "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X",
                             svm_id_resp, link_resp->status, link_resp->assigned_lak);
                    send_to_gui_socket(gui_buffer_main_loop);
                }

            } // if svm_id_resp valid
            pthread_mutex_unlock(&uvm_links_mutex);
        } // if uvq_dequeue


        // === БЛОК 3: ПРОВЕРКА ТАЙМАУТОВ ОЖИДАНИЯ ОТВЕТОВ НА КОМАНДЫ ПОДГОТОВКИ ===
        pthread_mutex_lock(&uvm_links_mutex);
        time_t now_timeout_check = time(NULL);
        for (int k_to = 0; k_to < num_svms_in_config; ++k_to) {
            if (!config.svm_config_loaded[k_to]) continue;
            UvmSvmLink *link_check_to = &svm_links[k_to];
            
            if (link_check_to->status == UVM_LINK_ACTIVE && // Проверяем только для активных TCP
                link_check_to->prep_state != PREP_STATE_PREPARATION_COMPLETE &&
                link_check_to->prep_state != PREP_STATE_FAILED &&
                link_check_to->prep_state != PREP_STATE_NOT_STARTED && // Не ждем ответа, если еще не отправили первую команду
                link_check_to->last_command_sent_time > 0) { // Убедимся, что команда была отправлена

                time_t current_timeout_val_s = 0;
                const char* cmd_name_for_timeout_event = "UnknownCmd";
                MessageType expected_reply_for_timeout_event = (MessageType)0;

                switch(link_check_to->prep_state) {
                    case PREP_STATE_AWAITING_CONFIRM_INIT_REPLY: // <--- ИЗМЕНЕНО
                        current_timeout_val_s = TIMEOUT_CONFIRM_INIT_S_MAIN;
                        cmd_name_for_timeout_event = "InitChannel";
                        expected_reply_for_timeout_event = MESSAGE_TYPE_CONFIRM_INIT;
                        break;
                    case PREP_STATE_AWAITING_PODTV_KONTROL_REPLY: // <--- ИЗМЕНЕНО
                        current_timeout_val_s = TIMEOUT_CONFIRM_KONTROL_S_MAIN;
                        cmd_name_for_timeout_event = "ProvestiKontrol";
                        expected_reply_for_timeout_event = MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA;
                        break;
                    case PREP_STATE_AWAITING_REZ_KONTROL_REPLY: // <--- ИЗМЕНЕНО
                        current_timeout_val_s = TIMEOUT_RESULTS_KONTROL_S_MAIN;
                        cmd_name_for_timeout_event = "VydatRezultatyKontrolya";
                        expected_reply_for_timeout_event = MESSAGE_TYPE_RESULTATY_KONTROLYA;
                        break;
                    case PREP_STATE_AWAITING_LINE_STATUS_REPLY: // <--- ИЗМЕНЕНО
                        current_timeout_val_s = TIMEOUT_LINE_STATUS_S_MAIN;
                        cmd_name_for_timeout_event = "VydatSostoyanieLinii";
                        expected_reply_for_timeout_event = MESSAGE_TYPE_SOSTOYANIE_LINII;
                        break;
                    default: break;
                }

                if (current_timeout_val_s > 0 && (now_timeout_check - link_check_to->last_command_sent_time) > current_timeout_val_s) {
                    fprintf(stderr, "UVM Main (SVM %d): ТАЙМАУТ! Ожидался ответ типа %d на команду '%s' (тип %u).\n",
                           k_to, expected_reply_for_timeout_event, cmd_name_for_timeout_event, link_check_to->last_sent_prep_cmd_type);
                    link_check_to->prep_state = PREP_STATE_FAILED;
                    link_check_to->status = UVM_LINK_FAILED;
                    link_check_to->response_timeout_detected = true;
                    
                    snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop),
                             "EVENT;SVM_ID:%d;Type:ResponseTimeout;Details:Cmd=%s,ExpectedReplyType=%d",
                             k_to, cmd_name_for_timeout_event, expected_reply_for_timeout_event);
                    send_to_gui_socket(gui_buffer_main_loop);
                    
                    snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop),
                             "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X",
                             k_to, UVM_LINK_FAILED, link_check_to->assigned_lak);
                    send_to_gui_socket(gui_buffer_main_loop);
                    processed_something_this_iteration = true;
                }
            }
        }
        pthread_mutex_unlock(&uvm_links_mutex);

        // === БЛОК 4: ПРОВЕРКА KEEP-ALIVE (для общего таймаута неактивности) ===
        time_t now_keep_alive_check = time(NULL);
        pthread_mutex_lock(&uvm_links_mutex);
        for (int ka_idx = 0; ka_idx < num_svms_in_config; ++ka_idx) {
            if (!config.svm_config_loaded[ka_idx]) continue;
            UvmSvmLink *link_ka = &svm_links[ka_idx];
            if (link_ka->status == UVM_LINK_ACTIVE && // Только для активных
                link_ka->prep_state != PREP_STATE_FAILED && // И не в ошибке подготовки
                link_ka->last_activity_time > 0 &&
                (now_keep_alive_check - link_ka->last_activity_time) > config.uvm_keepalive_timeout_sec)
            {
                fprintf(stderr, "UVM Main: Keep-Alive TIMEOUT detected for SVM ID %d! (Last activity %ld s ago)\n",
                        ka_idx, (now_keep_alive_check - link_ka->last_activity_time));
                link_ka->status = UVM_LINK_FAILED;
                link_ka->prep_state = PREP_STATE_FAILED; // Также помечаем подготовку как FAILED
                link_ka->timeout_detected = true;
                if (link_ka->connection_handle >= 0) {
                    shutdown(link_ka->connection_handle, SHUT_RDWR); // Пытаемся уведомить receiver
                }
                snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop), "EVENT;SVM_ID:%d;Type:KeepAliveTimeout;Details:No activity for %d sec", ka_idx, config.uvm_keepalive_timeout_sec);
                send_to_gui_socket(gui_buffer_main_loop);
                snprintf(gui_buffer_main_loop, sizeof(gui_buffer_main_loop), "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X", ka_idx, UVM_LINK_FAILED, link_ka->assigned_lak);
                send_to_gui_socket(gui_buffer_main_loop);
                processed_something_this_iteration = true;
            }
        }
        pthread_mutex_unlock(&uvm_links_mutex);

        // Если ничего не произошло на этой итерации, небольшая пауза
        if (!processed_something_this_iteration && uvm_keep_running) {
            usleep(20000); // 20 мс
        }
    } // end while (uvm_keep_running)


cleanup_connections: // Метка для goto при ошибках на ранних этапах
    printf("UVM: Завершение работы и очистка ресурсов...\n");
    uvm_keep_running = false; // Устанавливаем флаг для всех потоков, если еще не установлен

    // Сигналим очередям, чтобы потоки sender/receiver могли завершиться, если ждут
    if (uvm_outgoing_request_queue) queue_req_shutdown(uvm_outgoing_request_queue);
    if (uvm_incoming_response_queue) uvq_shutdown(uvm_incoming_response_queue);

    // Ожидаем завершения потока Sender
    if (sender_tid != 0) {
        pthread_join(sender_tid, NULL);
        printf("UVM: Sender thread joined.\n");
    }

    // Ожидаем завершения потоков Receiver'ов
    pthread_mutex_lock(&uvm_links_mutex); // Блокируем для безопасного доступа к svm_links
    for (int i = 0; i < num_svms_in_config; ++i) {
        if (svm_links[i].receiver_tid != 0) {
            // Receiver должен сам завершиться, если uvm_keep_running=false или его сокет закрыт
            pthread_join(svm_links[i].receiver_tid, NULL);
            printf("UVM: Receiver thread for SVM ID %d joined.\n", i);
            svm_links[i].receiver_tid = 0;
        }
        // Закрываем соединения и уничтожаем интерфейсы (если еще не сделано)
        if (svm_links[i].io_handle) {
            if (svm_links[i].connection_handle >= 0) {
                svm_links[i].io_handle->disconnect(svm_links[i].io_handle, svm_links[i].connection_handle);
            }
            svm_links[i].io_handle->destroy(svm_links[i].io_handle);
            svm_links[i].io_handle = NULL;
            svm_links[i].connection_handle = -1;
            svm_links[i].status = UVM_LINK_INACTIVE;
        }
    }
    pthread_mutex_unlock(&uvm_links_mutex);

    // Завершаем поток GUI сервера
    if (gui_server_tid != 0) {
        // Закрываем слушающий сокет GUI, чтобы accept вышел
        pthread_mutex_lock(&gui_socket_mutex);
        if (gui_listen_fd >= 0) { close(gui_listen_fd); gui_listen_fd = -1; }
        if (gui_client_fd >= 0) { shutdown(gui_client_fd, SHUT_RDWR); /*close(gui_client_fd);*/ gui_client_fd = -1; } // Разбудить, если он спит
        pthread_mutex_unlock(&gui_socket_mutex);
        pthread_join(gui_server_tid, NULL);
        printf("UVM: GUI server thread joined.\n");
    }

cleanup_queues:
    if (uvm_outgoing_request_queue && uvm_outgoing_request_queue->shutdown == false) queue_req_shutdown(uvm_outgoing_request_queue); // На всякий случай
    if (uvm_incoming_response_queue && uvm_incoming_response_queue->shutdown == false) uvq_shutdown(uvm_incoming_response_queue); // На всякий случай
    if (uvm_outgoing_request_queue) queue_req_destroy(uvm_outgoing_request_queue);
    if (uvm_incoming_response_queue) uvq_destroy(uvm_incoming_response_queue);

// cleanup_sync: // Метка не используется, т.к. инициализация мьютексов происходит раньше
    pthread_mutex_destroy(&uvm_links_mutex);
    pthread_mutex_destroy(&uvm_send_counter_mutex);
    pthread_cond_destroy(&uvm_all_sent_cond);
    pthread_mutex_destroy(&gui_socket_mutex);

    printf("UVM: Очистка завершена. Программа штатно завершает работу.\n");
    return 0;
} // конец main