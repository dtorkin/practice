/*
 * svm/svm_processor.c
 * Описание: Поток-обработчик для ОДНОГО SVM.
 * (Возвращено к одно-экземплярной модели)
 */
#include <stdio.h>
#include <stdlib.h> // Для free, malloc
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <arpa/inet.h> // Для ntohs
#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../utils/ts_queue.h" // Используем обычную очередь
#include "svm_handlers.h"
#include "svm_timers.h" // Для timer_thread_running

// Внешние глобальные переменные
extern IOInterface *io_svm; // Нужен для передачи в handler
extern int global_client_handle; // Нужен для передачи в handler
extern ThreadSafeQueue *svm_incoming_queue;
extern ThreadSafeQueue *svm_outgoing_queue;
extern volatile bool keep_running;

void* processor_thread_func(void* arg) {
     (void)arg;
     printf("SVM Processor thread started.\n");
     Message processingMessage; // Буфер для Message

     while(true) {
        // Пытаемся извлечь Message из входящей очереди
        if (!queue_dequeue(svm_incoming_queue, &processingMessage)) {
            if (!keep_running && svm_incoming_queue->count == 0) {
                 printf("Processor Thread: Incoming queue empty and shutdown. Exiting.\n");
                 break; // Корректный выход
            }
            // Иначе ложное пробуждение или очередь закрыта, но не пуста
            if (keep_running) usleep(10000);
            continue; // Продолжаем, пока очередь не опустеет или keep_running=false
        }

        // Сообщение успешно извлечено
        MessageHandler handler = message_handlers[processingMessage.header.message_type];
        Message* responseMessagePtr = NULL; // Указатель на ответ

        if (handler != NULL) {
            // Вызываем обработчик, передавая IO, хэндл и сообщение
            responseMessagePtr = handler(io_svm, global_client_handle, &processingMessage);
        } else {
            printf("Processor Thread: Unknown message type: %u (number %u)\n",
                   processingMessage.header.message_type,
                   get_full_message_number(&processingMessage.header));
        }

        if (responseMessagePtr != NULL) {
            // Копируем ответ в исходящую очередь
            if (!queue_enqueue(svm_outgoing_queue, responseMessagePtr)) {
                fprintf(stderr, "Processor Thread: Failed to enqueue response (type %u) to outgoing queue.\n", responseMessagePtr->header.message_type);
                free(responseMessagePtr); // Освобождаем память, раз не смогли передать
                if (keep_running) {
                    // Сигналим о проблеме
                    keep_running = false;
                    if (svm_incoming_queue) queue_shutdown(svm_incoming_queue);
                    if (svm_outgoing_queue) queue_shutdown(svm_outgoing_queue);
                    stop_timer_thread();
                }
                break; // Выходим из цикла
            } else {
                 // printf("Processor Thread: Response (type %u) enqueued.\n", responseMessagePtr->header.message_type);
                 // Освобождаем память ПОСЛЕ успешного копирования в очередь
                 free(responseMessagePtr);
            }
        }
     } // end while

     printf("SVM Processor thread finished.\n");
     // Сигнализируем Sender'у, что новых сообщений точно не будет (если он еще работает)
     if (svm_outgoing_queue && !svm_outgoing_queue->shutdown) {
         printf("Processor Thread: Shutting down outgoing queue...\n");
         queue_shutdown(svm_outgoing_queue);
     }
     return NULL;
}