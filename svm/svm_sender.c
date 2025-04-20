/*
 * svm/svm_sender.c
 * Описание: Поток-отправитель для ОДНОГО SVM.
 * (Возвращено к одно-экземплярной модели)
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h> // Для shutdown
#include <errno.h>
#include "../io/io_common.h"
#include "../utils/ts_queue.h" // Используем обычную очередь
#include "svm_timers.h" // Для stop_timer_thread

// Внешние глобальные переменные
extern IOInterface *io_svm;
extern int global_client_handle;
extern ThreadSafeQueue *svm_outgoing_queue;
extern ThreadSafeQueue *svm_incoming_queue; // Нужен для shutdown при ошибке
extern volatile bool keep_running;

void* sender_thread_func(void* arg) {
    (void)arg;
    if (global_client_handle < 0) {
         fprintf(stderr,"Sender Thread: Invalid client handle.\n");
         return NULL;
    }
    printf("SVM Sender thread started.\n");
    Message messageToSend; // Буфер для Message

    while(true) {
        // Пытаемся извлечь Message из исходящей очереди
        if (!queue_dequeue(svm_outgoing_queue, &messageToSend)) {
            if (!keep_running && svm_outgoing_queue->count == 0) {
                 printf("Sender Thread: Outgoing queue empty and shutdown. Exiting.\n");
                 break; // Корректный выход
            }
            if (keep_running) usleep(10000);
            continue; // Продолжаем, пока очередь не опустеет или keep_running=false
        }

        // Сообщение успешно извлечено
        // Отправляем сообщение
        if (send_protocol_message(io_svm, global_client_handle, &messageToSend) != 0) {
            if (keep_running) { // Логируем ошибку только если не штатное завершение
                fprintf(stderr, "Sender Thread: Error sending message (type %u). Initiating shutdown.\n", messageToSend.header.message_type);
                keep_running = false; // Инициируем остановку
                // Будим остальные потоки
                stop_timer_thread();
                if (svm_incoming_queue) queue_shutdown(svm_incoming_queue);
                if (svm_outgoing_queue) queue_shutdown(svm_outgoing_queue);
                if (global_client_handle >= 0) shutdown(global_client_handle, SHUT_RDWR);
            }
            break; // Выходим из цикла при ошибке отправки
        }
        // Память из malloc в handler'е была освобождена в processor'е после enqueue
    } // end while

    printf("SVM Sender thread finished.\n");
    return NULL;
}