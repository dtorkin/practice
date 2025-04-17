/*
 * svm/svm_processor.c
 */
#include <stdio.h>
#include <stdlib.h> // Для free (если будем освобождать сообщение здесь при ошибке enqueue)
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../utils/ts_queue.h"
#include "svm_handlers.h"
#include "svm_timers.h" // Для keep_running

// Внешние переменные
extern IOInterface *io_svm;
extern int global_client_handle;
extern ThreadSafeQueue *svm_incoming_queue;
extern ThreadSafeQueue *svm_outgoing_queue;
extern volatile bool keep_running;

void* processor_thread_func(void* arg) {
     (void)arg;
     printf("SVM Processor thread started.\n");
     Message processingMessage; // Структура для извлеченного сообщения

     while(true) {
        // Пытаемся извлечь сообщение из входящей очереди
        if (!queue_dequeue(svm_incoming_queue, &processingMessage)) {
            if (!keep_running && svm_incoming_queue->count == 0) {
                 printf("Processor Thread: Входящая очередь пуста и получен сигнал завершения.\n");
                 break; // Корректный выход
            }
            if(keep_running) {
                 // Возможно, ложное пробуждение или ошибка dequeue
                 usleep(10000); // Пауза
                 continue;
            } else {
                // keep_running=false, но очередь еще не пуста
                continue; // Продолжаем обрабатывать оставшиеся
            }
        }

        // Сообщение успешно извлечено (processingMessage содержит копию)
        MessageHandler handler = message_handlers[processingMessage.header.message_type];
        Message* responseMessagePtr = NULL; // Указатель на ответное сообщение

		if (handler != NULL) {
            // Вызываем обработчик. Он вернет malloc'нутый ответ или NULL.
            responseMessagePtr = handler(io_svm, global_client_handle, &processingMessage);
		} else {
			printf("Processor Thread: Неизвестный тип сообщения: %u (номер %u)\n",
                   processingMessage.header.message_type,
                   get_full_message_number(&processingMessage.header));
		}

        // Если обработчик вернул ответное сообщение
        if (responseMessagePtr != NULL) {
            // Копируем ответ в очередь отправки
            // Передаем КОПИЮ в очередь отправки
            if (!queue_enqueue(svm_outgoing_queue, responseMessagePtr)) {
                // Ошибка добавления в очередь (вероятно, она закрыта)
                fprintf(stderr, "Processor Thread: Не удалось добавить ответ (тип %u) в исходящую очередь.\n", responseMessagePtr->header.message_type);
                // Освобождаем память, раз не смогли передать
                free(responseMessagePtr);
                if (keep_running) {
                    // Если мы еще должны работать, это проблема, сигналим о завершении
                    keep_running = false;
                    queue_shutdown(svm_incoming_queue); // На всякий случай
                    queue_shutdown(svm_outgoing_queue);
                }
                break; // Выходим из цикла
            } else {
                 // printf("Processor Thread: Ответ (тип %u) добавлен в исходящую очередь.\n", responseMessagePtr->header.message_type);
                 // Память responseMessagePtr будет освобождена в Sender Thread после отправки
                 free(responseMessagePtr); // Освобождаем память ПОСЛЕ успешного КОПИРОВАНИЯ в очередь
            }
        }
        // Если responseMessagePtr == NULL, ничего не делаем
     }

     printf("SVM Processor thread finished.\n");
     // Сигнализируем Sender'у, что новых сообщений точно не будет
     if (svm_outgoing_queue && !svm_outgoing_queue->shutdown) {
        printf("Processor Thread: Отправка shutdown в outgoing queue...\n");
        queue_shutdown(svm_outgoing_queue);
     }
     return NULL;
}