/*
 * uvm/uvm_sender.c
 * Описание: Поток UVM для отправки сообщений разным SVM.
 */
#include "uvm_sender.h" // (Если есть)
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>

#include "../utils/ts_queue_req.h" // Очередь запросов
#include "../io/io_common.h"
#include "uvm_types.h"
#include "../config/config.h" // Для MAX_SVM_INSTANCES

// Внешние переменные из uvm_main.c
extern ThreadSafeReqQueue *uvm_outgoing_request_queue;
extern UvmSvmLink svm_links[MAX_SVM_INSTANCES];
extern pthread_mutex_t uvm_links_mutex; // Мьютекс для доступа к svm_links
extern volatile bool uvm_keep_running;
extern volatile int uvm_outstanding_sends; // Счетчик для синхронизации
extern pthread_cond_t uvm_all_sent_cond;   // Условная переменная
extern pthread_mutex_t uvm_send_counter_mutex; // Мьютекс для счетчика

void* uvm_sender_thread_func(void* arg) {
    (void)arg;
    printf("UVM Sender thread started.\n");
    UvmRequest request;
    bool shutdown_req_received = false;

    while (!shutdown_req_received) {
        // Извлекаем запрос из очереди
        if (!queue_req_dequeue(uvm_outgoing_request_queue, &request)) {
			printf("UVM Sender: Забрал из очереди запрос для SVM %d, тип сообщения %d.\n", request.target_svm_id, request.message.header.message_type); // <-- ОТЛАДКА
            if (!uvm_keep_running && uvm_outgoing_request_queue->count == 0) {
                printf("Sender Thread: Request queue empty and shutdown signaled. Exiting.\n");
                break;
            }
            if(uvm_keep_running) usleep(10000); // Ложное пробуждение?
            continue; // Продолжаем, если работаем или очередь не пуста
        }

        // Обрабатываем запрос
        if (request.type == UVM_REQ_SHUTDOWN) {
            printf("Sender Thread: Received shutdown request.\n");
            shutdown_req_received = true;
            continue; // Выйдем из цикла на следующей итерации
        }

        if (request.type == UVM_REQ_SEND_MESSAGE) {
            int svm_id = request.target_svm_id;
            IOInterface *io = NULL;
            int handle = -1;
            bool is_active = false;

            // Получаем данные соединения под мьютексом
            pthread_mutex_lock(&uvm_links_mutex);
            if (svm_id >= 0 && svm_id < MAX_SVM_INSTANCES && svm_links[svm_id].status == UVM_LINK_ACTIVE) {
                io = svm_links[svm_id].io_handle;
                handle = svm_links[svm_id].connection_handle;
                is_active = true;
            }
            pthread_mutex_unlock(&uvm_links_mutex);

			if (is_active && io && handle >= 0) {
				if (send_protocol_message(io, handle, &request.message) != 0) {
					fprintf(stderr, "UVM Sender: ОШИБКА ФИЗИЧЕСКОЙ отправки сообщения тип %u SVM %d.\n", request.message.header.message_type, svm_id); // <-- ОТЛАДКА
				} else {
					//printf("UVM Sender: Сообщение тип %u УСПЕШНО ФИЗИЧЕСКИ отправлено SVM %d.\n", request.message.header.message_type, svm_id); // <-- ОТЛАДКА
				}
			} else if (is_active) {
				 fprintf(stderr, "UVM Sender: SVM %d активен, но io/handle невалидны. Пропуск отправки.\n", svm_id); // <-- ОТЛАДКА
			} else {
				 fprintf(stderr, "UVM Sender: SVM %d НЕ активен. Пропуск отправки.\n", svm_id); // <-- ОТЛАДКА
			}

            // Уменьшаем счетчик ожидающих отправки и сигналим Main, если он ждет
            pthread_mutex_lock(&uvm_send_counter_mutex);
            if (uvm_outstanding_sends > 0) {
                uvm_outstanding_sends--;
                 //printf("UVM Sender: uvm_outstanding_sends уменьшен до %d (после обработки запроса тип %d для SVM %d).\n", uvm_outstanding_sends, request.message.header.message_type, request.target_svm_id); // <-- ОТЛАДКА
                if (uvm_outstanding_sends == 0) {
                    //printf("Sender Thread: All pending messages sent, signaling Main.\n");
                    pthread_cond_signal(&uvm_all_sent_cond);
                }
            }
            pthread_mutex_unlock(&uvm_send_counter_mutex);

        } else {
            fprintf(stderr, "UVM Sender: ВНИМАНИЕ! Попытка уменьшить uvm_outstanding_sends, когда он уже 0 или меньше.\n");
        }
    } // end while

    printf("UVM Sender thread finished.\n");
    // Убедимся, что Main не застрянет в ожидании, если мы вышли из-за shutdown
    pthread_mutex_lock(&uvm_send_counter_mutex);
    if (uvm_outstanding_sends > 0) {
        printf("Sender Thread: Exiting with %d outstanding sends (due to shutdown).\n", uvm_outstanding_sends);
        uvm_outstanding_sends = 0; // Сбрасываем счетчик
        pthread_cond_signal(&uvm_all_sent_cond); // Разбудить Main на всякий случай
    }
     pthread_mutex_unlock(&uvm_send_counter_mutex);
    return NULL;
}