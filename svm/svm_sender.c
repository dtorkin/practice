/*
 * svm/svm_sender.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h> // <-- ДОБАВЛЕНО для shutdown, SHUT_RDWR
#include "../io/io_common.h"
#include "../utils/ts_queue.h"
#include "svm_timers.h" // Для stop_timer_thread

// Внешние переменные
extern IOInterface *io_svm;
extern int global_client_handle;
extern ThreadSafeQueue *svm_outgoing_queue;
extern ThreadSafeQueue *svm_incoming_queue; // Нужен для shutdown при ошибке
extern volatile bool keep_running;


void* sender_thread_func(void* arg) {
    (void)arg;
    printf("SVM Sender thread started.\n");
    Message messageToSend; // Буфер для извлеченного сообщения

    while(true) {
        // Пытаемся извлечь сообщение из исходящей очереди
        if (!queue_dequeue(svm_outgoing_queue, &messageToSend)) {
            // Очередь пуста и закрыта, или ошибка
            if (!keep_running && svm_outgoing_queue->count == 0) {
                 printf("Sender Thread: Исходящая очередь пуста и получен сигнал завершения.\n");
                 break; // Корректный выход
            }
             if(keep_running) {
                 usleep(10000); // Ложное пробуждение или ошибка, ждем
                 continue;
             } else {
                 break; // Выход, если shutdown и ошибка dequeue
             }
        }

        // Сообщение успешно извлечено
        // printf("DEBUG: Sender dequeued message type %u\n", messageToSend.header.message_type);

        // Отправляем сообщение
        if (send_protocol_message(io_svm, global_client_handle, &messageToSend) != 0) {
            fprintf(stderr, "Sender Thread: Ошибка отправки сообщения (тип %u). Завершение потока.\n", messageToSend.header.message_type);
            // Сигнализируем другим потокам об ошибке
            if(keep_running) {
                keep_running = false;
                // Будим остальные потоки
                stop_timer_thread();
                if (svm_incoming_queue) queue_shutdown(svm_incoming_queue);
                if (svm_outgoing_queue) queue_shutdown(svm_outgoing_queue); // Закрываем и свою очередь
                 // Пытаемся закрыть сокет, чтобы разбудить receiver
                 // Используем shutdown из sys/socket.h
                if (global_client_handle >= 0) shutdown(global_client_handle, SHUT_RDWR); // <-- Теперь компилируется
            }
            break; // Выходим из цикла при ошибке отправки
        }
        // Память, выделенная в handler'е, была освобождена в processor'е после enqueue
    }

    printf("SVM Sender thread finished.\n");
    return NULL;
}