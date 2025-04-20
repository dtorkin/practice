/*
 * uvm/uvm_main.c
 *
 * Описание:
 * Основной файл UVM: инициализация, создание потоков (отправитель, приемник),
 * управление логикой взаимодействия через очереди.
 */

#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h" // Для get_full_message_number
#include "../io/io_common.h"           // Для receive_protocol_message (хотя он в receiver потоке)
#include "../io/io_interface.h"
#include "../config/config.h"
#include "../utils/ts_queue_req.h"     // Очередь запросов
#include "../utils/ts_queue.h"         // Очередь ответов/сообщений
#include "uvm_types.h"                 // Для UvmRequest

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // для strcasecmp
#include <unistd.h>  // для sleep/usleep, close
#include <arpa/inet.h> // Для inet_ntop и т.д. если нужно будет выводить IP
#include <netinet/in.h>
#include <sys/socket.h> // Для shutdown, SHUT_RDWR
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>   // Для clock_gettime, time

// --- Глобальные переменные для UVM ---
IOInterface *io_uvm = NULL;
ThreadSafeReqQueue *uvm_outgoing_request_queue = NULL;
ThreadSafeQueue    *uvm_incoming_response_queue = NULL;
volatile bool uvm_keep_running = true;
int uvm_comm_handle = -1; // Дескриптор соединения

// --- Глобальные переменные для синхронизации Main и Sender ---
pthread_mutex_t uvm_send_counter_mutex;
pthread_cond_t  uvm_send_counter_cond;
volatile int    uvm_outstanding_sends = 0; // Счетчик неотправленных сообщений

// --- Прототипы потоков ---
extern void* uvm_sender_thread_func(void* arg);
extern void* uvm_receiver_thread_func(void* arg);

// --- Обработчик сигналов ---
void uvm_handle_shutdown_signal(int sig) {
    const char msg_int[] = "\nUVM: Получен сигнал SIGINT. Завершение...\n";
    const char msg_term[] = "\nUVM: Получен сигнал SIGTERM. Завершение...\n";
    const char msg_unknown[] = "\nUVM: Получен неизвестный сигнал. Завершение...\n";
    const char *msg_ptr = (sig == SIGINT) ? msg_int : (sig == SIGTERM ? msg_term : msg_unknown);
    write(STDOUT_FILENO, msg_ptr, strlen(msg_ptr));

    uvm_keep_running = false;
    // Будим потоки, которые могут спать
    if (uvm_outgoing_request_queue) queue_req_shutdown(uvm_outgoing_request_queue);
    if (uvm_incoming_response_queue) queue_shutdown(uvm_incoming_response_queue);
    // Закрытие сокета произойдет в cleanup в main
}

// --- Вспомогательная функция для отправки запроса ---
bool send_uvm_request(UvmRequestType type, uint8_t param1) {
    if (!uvm_outgoing_request_queue || !uvm_keep_running) return false;
    UvmRequest req;
    memset(&req, 0, sizeof(req)); // Обнуляем структуру
    req.type = type;
    if (type == UVM_REQ_PROVESTI_KONTROL) req.tk_param = param1;
    else if (type == UVM_REQ_VYDAT_REZULTATY) req.vpk_param = param1;

    pthread_mutex_lock(&uvm_send_counter_mutex);
    if (!queue_req_enqueue(uvm_outgoing_request_queue, &req)) {
        pthread_mutex_unlock(&uvm_send_counter_mutex);
        fprintf(stderr, "UVM Main: Не удалось добавить запрос %d в очередь.\n", type);
        if (uvm_keep_running) uvm_keep_running = false;
        return false;
    }
    uvm_outstanding_sends++;
    pthread_mutex_unlock(&uvm_send_counter_mutex);
    return true;
}

// --- Вспомогательная функция для ожидания ответа ---
bool wait_for_response(MessageType expected_type, Message *response_msg, int timeout_sec) {
    if (!uvm_incoming_response_queue || !uvm_keep_running) return false;

    time_t start_time = time(NULL);
    while (uvm_keep_running) {
        if (queue_dequeue(uvm_incoming_response_queue, response_msg)) {
            if (response_msg->header.message_type == expected_type) {
                return true; // Получили нужный ответ
            } else {
                printf("UVM Main: Получено неожиданное сообщение тип %u (ожидалось %u).\n",
                       response_msg->header.message_type, expected_type);
                // TODO: Обработать или проигнорировать
            }
        } else {
             if (!uvm_keep_running && uvm_incoming_response_queue->count == 0) {
                 fprintf(stderr,"UVM Main: Очередь ответов закрыта во время ожидания типа %u.\n", expected_type);
                 return false;
             }
             // Иначе ложное пробуждение или ошибка dequeue - продолжаем ждать
        }

        if (timeout_sec > 0 && (time(NULL) - start_time) > timeout_sec) {
            fprintf(stderr, "UVM Main: Таймаут ожидания ответа типа %u.\n", expected_type);
            return false;
        }
        usleep(50000); // Пауза, чтобы не грузить CPU
    }
    fprintf(stderr,"UVM Main: Ожидание ответа типа %u прервано сигналом завершения.\n", expected_type);
    return false; // Вышли из цикла из-за uvm_keep_running == false
}


int main(int argc, char* argv[] ) {
    AppConfig config;
    pthread_t sender_tid = 0, receiver_tid = 0;
    int exit_code = EXIT_SUCCESS; // Код возврата программы

	// --- Инициализация ---
	printf("UVM: Загрузка конфигурации...\n");
    if (load_config("config.ini", &config) != 0) { exit(EXIT_FAILURE); } // Вызов с 2 аргументами

    if (pthread_mutex_init(&uvm_send_counter_mutex, NULL) != 0) { perror("UVM: Failed to init send counter mutex"); exit(EXIT_FAILURE); }
    if (pthread_cond_init(&uvm_send_counter_cond, NULL) != 0) { perror("UVM: Failed to init send counter cond var"); pthread_mutex_destroy(&uvm_send_counter_mutex); exit(EXIT_FAILURE); }

	printf("UVM: Создание интерфейса типа '%s'...\n", config.interface_type);
    if (strcasecmp(config.interface_type, "ethernet") == 0) { io_uvm = create_ethernet_interface(&config.uvm_ethernet_target); }
    else if (strcasecmp(config.interface_type, "serial") == 0) { io_uvm = create_serial_interface(&config.serial); }
    else { fprintf(stderr, "UVM: Неподдерживаемый тип интерфейса '%s'.\n", config.interface_type); goto cleanup; } // Используем cleanup
    if (!io_uvm) { fprintf(stderr, "UVM: Не удалось создать IOInterface.\n"); goto cleanup; } // Используем cleanup

    uvm_outgoing_request_queue = queue_req_create(50);
    uvm_incoming_response_queue = queue_create(50);
    if (!uvm_outgoing_request_queue || !uvm_incoming_response_queue) { fprintf(stderr, "UVM: Не удалось создать очереди.\n"); goto cleanup; }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = uvm_handle_shutdown_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

	// --- Выбор режима работы ---
	RadarMode selectedMode = MODE_DR; // Режим по умолчанию - ДР
	if (argc > 1) {
		if (strcmp(argv[1], "OR") == 0) selectedMode = MODE_OR;
		else if (strcmp(argv[1], "OR1") == 0) selectedMode = MODE_OR1;
		else if (strcmp(argv[1], "DR") == 0) selectedMode = MODE_DR;
		else if (strcmp(argv[1], "VR") == 0) selectedMode = MODE_VR;
		else {
			fprintf(stderr, "Неверный режим работы. Используйте OR, OR1, DR или VR.\n");
			exit(EXIT_FAILURE);
		}
	}


	// --- Подключение к SVM ---
    printf("UVM: Подключение к SVM через %s...\n", config.interface_type);
    uvm_comm_handle = io_uvm->connect(io_uvm);
	if (uvm_comm_handle < 0) { fprintf(stderr, "UVM: Ошибка подключения к SVM.\n"); goto cleanup; }
	printf("UVM: Успешно подключено (handle: %d)\n", uvm_comm_handle);

	printf("Выбран режим работы: %s\n",
           (selectedMode == MODE_OR) ? "OR" :
           (selectedMode == MODE_OR1) ? "OR1" :
           (selectedMode == MODE_DR) ? "DR" : "VR");


	// --- Запуск потоков ---
    printf("UVM: Запуск потоков Sender и Receiver...\n");
    uvm_keep_running = true; // Сбрасываем флаг перед запуском
    if (pthread_create(&sender_tid, NULL, uvm_sender_thread_func, NULL) != 0) { perror("UVM: Failed to create sender thread"); goto cleanup_threads; }
    if (pthread_create(&receiver_tid, NULL, uvm_receiver_thread_func, NULL) != 0) { perror("UVM: Failed to create receiver thread"); goto cleanup_threads; }
    printf("UVM: Потоки запущены.\n");


	// --- ВЗАИМОДЕЙСТВИЕ С SVM ---
    Message response_msg;

	// --- ПОДГОТОВКА К СЕАНСУ НАБЛЮДЕНИЯ ---
	printf("\n--- Подготовка к сеансу наблюдения ---\n");
    bool seq_ok = true; // Флаг успешного выполнения последовательности

    seq_ok &= send_uvm_request(UVM_REQ_INIT_CHANNEL, 0);
    if (seq_ok && !wait_for_response(MESSAGE_TYPE_CONFIRM_INIT, &response_msg, 5)) {
        fprintf(stderr, "UVM: Не получен ответ на Init Channel.\n"); seq_ok = false;
    }
    if (seq_ok) {
        ConfirmInitBody* confirmInitBody = (ConfirmInitBody*)response_msg.body;
        printf("Получено подтверждение инициализации: LAK=0x%02X, BCB=0x%08X\n", confirmInitBody->lak, confirmInitBody->bcb);
        sleep(1);
    }

    if (seq_ok) {
        uint8_t tk_request = 0x01;
        seq_ok &= send_uvm_request(UVM_REQ_PROVESTI_KONTROL, tk_request);
        if (seq_ok && !wait_for_response(MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA, &response_msg, 5)) {
             fprintf(stderr, "UVM: Не получен ответ на Provesti Kontrol.\n"); seq_ok = false;
        }
        if (seq_ok) {
            PodtverzhdenieKontrolyaBody* podtverzhdenieKontrolyaBody = (PodtverzhdenieKontrolyaBody*)response_msg.body;
            printf("Получено подтверждение контроля: LAK=0x%02X, TK=0x%02X, BCB=0x%08X\n", podtverzhdenieKontrolyaBody->lak, podtverzhdenieKontrolyaBody->tk, podtverzhdenieKontrolyaBody->bcb);
            sleep(1);
        }
    }

     if (seq_ok) {
         uint8_t vpk_request = 0x0F;
         seq_ok &= send_uvm_request(UVM_REQ_VYDAT_REZULTATY, vpk_request);
         if (seq_ok && !wait_for_response(MESSAGE_TYPE_RESULTATY_KONTROLYA, &response_msg, 5)) {
             fprintf(stderr, "UVM: Не получен ответ на Vydat Rezultaty.\n"); seq_ok = false;
         }
         if (seq_ok) {
             RezultatyKontrolyaBody* rezultatyKontrolyaBody = (RezultatyKontrolyaBody*)response_msg.body;
             printf("Получены результаты контроля: LAK=0x%02X, RSK=0x%02X, VSK=0x%04X, BCB=0x%08X\n", rezultatyKontrolyaBody->lak, rezultatyKontrolyaBody->rsk, rezultatyKontrolyaBody->vsk, rezultatyKontrolyaBody->bcb);
             sleep(1);
         }
    }

    if (seq_ok) {
         seq_ok &= send_uvm_request(UVM_REQ_VYDAT_SOSTOYANIE, 0);
         if (seq_ok && !wait_for_response(MESSAGE_TYPE_SOSTOYANIE_LINII, &response_msg, 5)) {
             fprintf(stderr, "UVM: Не получен ответ на Vydat Sostoyanie.\n"); seq_ok = false;
         }
         if (seq_ok) {
             SostoyanieLiniiBody* sostoyanieLiniiBody = (SostoyanieLiniiBody*)response_msg.body;
             printf("Получено состояние линии: LAK=0x%02X, KLA=0x%04X, SLA=0x%08X, KSA=0x%04X, BCB=0x%08X\n",
                    sostoyanieLiniiBody->lak, sostoyanieLiniiBody->kla, sostoyanieLiniiBody->sla, sostoyanieLiniiBody->ksa, sostoyanieLiniiBody->bcb);
             sleep(1);
         }
    }

    if (!seq_ok) {
        fprintf(stderr, "UVM: Ошибка на этапе подготовки к наблюдению.\n");
        goto shutdown_threads;
    }

    // Ожидаем отправки всех сообщений подготовки к наблюдению
    pthread_mutex_lock(&uvm_send_counter_mutex);
    while(uvm_outstanding_sends > 0 && uvm_keep_running) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        pthread_cond_timedwait(&uvm_send_counter_cond, &uvm_send_counter_mutex, &ts);
    }
    pthread_mutex_unlock(&uvm_send_counter_mutex);
    if (!uvm_keep_running) goto shutdown_threads;


	// --- ПОДГОТОВКА К СЕАНСУ СЪЁМКИ ---
	printf("\n--- Подготовка к сеансу съемки - ");
    bool send_ok = true;

	if (selectedMode == MODE_DR) {
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_PARAM_SDR, 0);
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_PARAM_TSD, 0);
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_NAV_DANNYE, 0);
	} else if (selectedMode == MODE_VR) {
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_PARAM_SO, 0);
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_PARAM_3TSO, 0);
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_NAV_DANNYE, 0);
	} else { // OR or OR1
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_PARAM_SO, 0);
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_TIME_REF, 0);
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_REPER, 0);
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_PARAM_3TSO, 0);
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_REF_AZIMUTH, 0);
		send_ok &= send_uvm_request(UVM_REQ_PRIYAT_NAV_DANNYE, 0);
	}

    if(!send_ok) {
        fprintf(stderr, "UVM: Ошибка при добавлении сообщений подготовки к съемке в очередь.\n");
        goto shutdown_threads;
    }

    // --- ОЖИДАНИЕ ОТПРАВКИ ВСЕХ СООБЩЕНИЙ ПОДГОТОВКИ К СЪЕМКЕ ---
    printf("UVM Main: Ожидание завершения отправки сообщений подготовки к съемке...\n");
    pthread_mutex_lock(&uvm_send_counter_mutex);
    while(uvm_outstanding_sends > 0 && uvm_keep_running) {
        printf("UVM Main: Осталось отправить: %d\n", uvm_outstanding_sends);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        pthread_cond_timedwait(&uvm_send_counter_cond, &uvm_send_counter_mutex, &ts);
    }
    bool all_sent_before_stop = (uvm_outstanding_sends == 0);
    pthread_mutex_unlock(&uvm_send_counter_mutex);

    if (!uvm_keep_running) {
        fprintf(stderr, "UVM: Остановка во время ожидания отправки.\n");
        goto shutdown_threads;
    }

    if(all_sent_before_stop) {
        printf("UVM: Все сообщения подготовки к съемке отправлены.\n");
        printf("UVM: Ожидание асинхронных сообщений от SVM (или Ctrl+C для завершения)...\n");
        // Здесь UVM может перейти в режим ожидания/обработки СУБК, КО и т.д.
        // пока просто ждем сигнала завершения
        while(uvm_keep_running) {
            // Можно добавить обработку сообщений из uvm_incoming_response_queue, если SVM их шлет
            // if (queue_dequeue(uvm_incoming_response_queue, &response_msg)) { ... }
            sleep(1); // Просто ждем
        }
    } else {
         fprintf(stderr, "UVM: Не удалось отправить все сообщения подготовки к съемке.\n");
         exit_code = EXIT_FAILURE; // Устанавливаем код ошибки
    }


shutdown_threads: // Инициируем завершение потоков
    printf("\nUVM: Инициируем завершение потоков...\n");
    if (uvm_keep_running) uvm_keep_running = false;

    if (uvm_outgoing_request_queue) {
        UvmRequest shutdown_req = {.type = UVM_REQ_SHUTDOWN};
        queue_req_enqueue(uvm_outgoing_request_queue, &shutdown_req);
        queue_req_shutdown(uvm_outgoing_request_queue);
    }
    if (uvm_incoming_response_queue) queue_shutdown(uvm_incoming_response_queue);

    if (uvm_comm_handle >= 0) {
        shutdown(uvm_comm_handle, SHUT_RDWR);
    }

cleanup_threads: // Ожидание завершения потоков
    printf("UVM: Ожидание завершения потоков...\n");
    if (sender_tid != 0) pthread_join(sender_tid, NULL); // Проверяем перед join
    printf("UVM: Sender thread joined.\n");
    if (receiver_tid != 0) pthread_join(receiver_tid, NULL);
    printf("UVM: Receiver thread joined.\n");


cleanup: // Очистка ресурсов
	printf("UVM: Завершение работы и очистка ресурсов...\n");
    if (io_uvm != NULL) {
        if (uvm_comm_handle >= 0) {
            io_uvm->disconnect(io_uvm, uvm_comm_handle);
            printf("UVM: Соединение закрыто (handle: %d).\n", uvm_comm_handle);
        }
        io_uvm->destroy(io_uvm);
        printf("UVM: Интерфейс IO освобожден.\n");
    } else if (uvm_comm_handle >= 0) {
         close(uvm_comm_handle); // Если интерфейс не создан, но хэндл есть
    }
    if(uvm_outgoing_request_queue) queue_req_destroy(uvm_outgoing_request_queue);
    if(uvm_incoming_response_queue) queue_destroy(uvm_incoming_response_queue);
    pthread_mutex_destroy(&uvm_send_counter_mutex);
    pthread_cond_destroy(&uvm_send_counter_cond);

    printf("UVM: Очистка завершена.\n");
	return exit_code;
}