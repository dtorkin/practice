/*
 * uvm/uvm_receiver.c
 *
 * Описание:
 * Поток UVM, отвечающий за прием сообщений от SVM.
 */

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>

#include "../protocol/protocol_defs.h"
#include "../utils/ts_queue.h"      // Используем обычную очередь для входящих сообщений
#include "../utils/ts_queue_req.h"   // <-- ДОБАВЛЕНО для доступа к очереди запросов (для shutdown)
#include "../io/io_common.h"
#include "../io/io_interface.h"

// Внешние переменные из uvm_main
extern IOInterface *io_uvm;
extern ThreadSafeQueue *uvm_incoming_response_queue; // Очередь для ответов (Message)
extern volatile bool uvm_keep_running;
extern ThreadSafeReqQueue *uvm_outgoing_request_queue; // Очередь для запросов (UvmRequest)

void* uvm_receiver_thread_func(void* arg) {
    (void)arg;
    printf("UVM Receiver thread started (handle: %d).\n", io_uvm ? io_uvm->io_handle : -1);
    Message receivedMessage;

    while (uvm_keep_running) {
        if (!io_uvm || io_uvm->io_handle < 0) {
             if (uvm_keep_running) fprintf(stderr, "Receiver Thread: IO Interface не готов. Завершение.\n");
             if (uvm_keep_running) uvm_keep_running = false; // Сигналим другим
             break;
        }

        int recvStatus = receive_protocol_message(io_uvm, io_uvm->io_handle, &receivedMessage);

        if (!uvm_keep_running) break;

        if (recvStatus == -1) {
            if(uvm_keep_running) fprintf(stderr, "Receiver Thread: Ошибка получения сообщения. Завершение.\n");
             if (uvm_keep_running) uvm_keep_running = false;
             if (uvm_outgoing_request_queue) queue_req_shutdown(uvm_outgoing_request_queue); // Используем _req
			break;
		} else if (recvStatus == 1) {
			printf("Receiver Thread: Соединение закрыто SVM. Завершение.\n");
            if (uvm_keep_running) uvm_keep_running = false;
            if (uvm_outgoing_request_queue) queue_req_shutdown(uvm_outgoing_request_queue); // Используем _req
			break;
        } else if (recvStatus == -2) {
             usleep(10000);
             continue;
        } else { // Успех
            // Помещаем полученное сообщение (копию) в очередь ответов
            if (!queue_enqueue(uvm_incoming_response_queue, &receivedMessage)) {
                if (uvm_keep_running) {
                    fprintf(stderr, "Receiver Thread: Не удалось добавить сообщение во входящую очередь ответов.\n");
                    uvm_keep_running = false;
                }
                break;
            }
        }
	}

    printf("UVM Receiver thread finished.\n");
    // Сигнализируем Main thread и Sender, что больше не принимаем
    if (uvm_incoming_response_queue && !uvm_incoming_response_queue->shutdown) {
         queue_shutdown(uvm_incoming_response_queue);
    }
     if (uvm_outgoing_request_queue && !uvm_outgoing_request_queue->shutdown) { // <-- Теперь тип известен
         queue_req_shutdown(uvm_outgoing_request_queue); // Используем _req
     }

    return NULL;
}