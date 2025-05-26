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
                // Отправляем сообщение
                if (send_protocol_message(io, handle, &request.message) != 0) {
					fprintf(stderr, "UVM Sender: ОШИБКА отправки сообщения тип %u SVM %d.\n", request.message.header.message_type, svm_id); // <-- ОТЛАДКА
                    fprintf(stderr, "Sender Thread: Error sending message (type %u) to SVM ID %d (handle %d).\n",
                           request.message.header.message_type, svm_id, handle);
                    // Ошибка отправки - помечаем линк как неактивный
                    pthread_mutex_lock(&uvm_links_mutex);
                    if (svm_links[svm_id].status == UVM_LINK_ACTIVE) { // Доп. проверка
                         svm_links[svm_id].status = UVM_LINK_FAILED;
                         // Можно закрыть хэндл здесь или оставить Receiver'у/Main
                         if (svm_links[svm_id].connection_handle >= 0) {
                              shutdown(svm_links[svm_id].connection_handle, SHUT_RDWR);
                              // close(svm_links[svm_id].connection_handle); // Не закрываем здесь
                         }
                         printf("Sender Thread: Marked SVM Link %d as FAILED due to send error.\n", svm_id);
                         // Разбудить Receiver'а этого линка? (сложно без прямого доступа)
                         // Main поток должен будет обработать статус FAILED
                    }
                    pthread_mutex_unlock(&uvm_links_mutex);
                } else {
                     printf("UVM Sender: Сообщение тип %u УСПЕШНО отправлено SVM %d.\n", request.message.header.message_type, svm_id); // <-- ОТЛАДКА
                }
            } else {
                 fprintf(stderr, "Sender Thread: Cannot send message to SVM ID %d (inactive or invalid).\n", svm_id);
                 // Сообщение не отправлено, но счетчик все равно уменьшаем
            }

            // Уменьшаем счетчик ожидающих отправки и сигналим Main, если он ждет
            pthread_mutex_lock(&uvm_send_counter_mutex);
            if (uvm_outstanding_sends > 0) {
                uvm_outstanding_sends--;
                 //printf("Sender Thread: Decremented outstanding sends to %d\n", uvm_outstanding_sends);
                if (uvm_outstanding_sends == 0) {
                    //printf("Sender Thread: All pending messages sent, signaling Main.\n");
                    pthread_cond_signal(&uvm_all_sent_cond);
                }
            }
            pthread_mutex_unlock(&uvm_send_counter_mutex);

        } else {
            printf("Sender Thread: Received unhandled request type %d.\n", request.type);
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