/*
 * svm/svm_processor.c
 *
 * Описание:
 * Реализация потока-обработчика для ОДНОГО экземпляра SVM.
 * Читает сообщения из входящей очереди экземпляра, вызывает обработчик
 * и помещает ответ (если есть) в ОБЩУЮ исходящую очередь.
 * ВРЕМЕННАЯ РЕАЛИЗАЦИЯ: 1 поток на 1 экземпляр.
 */
#include <stdio.h>
#include <stdlib.h> // Для free, malloc
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h> // Для memcpy
#include "../protocol/protocol_defs.h"
#include "../protocol/message_utils.h"
#include "../utils/ts_queue.h"
#include "../utils/ts_queued_msg_queue.h"
#include "svm_handlers.h"
#include "svm_timers.h" // Для global_timer_keep_running
#include "svm_types.h"  // Для SvmInstance, QueuedMessage

// Внешние переменные (доступны из main)
extern ThreadSafeQueuedMsgQueue *svm_outgoing_queue; // Общая исходящая очередь
// extern SvmInstance svm_instances[MAX_SVM_INSTANCES]; // Не нужен прямой доступ к массиву здесь
extern volatile bool global_timer_keep_running; // Глобальный флаг остановки

void* processor_thread_func(void* arg) {
    SvmInstance *instance = (SvmInstance*)arg;
     if (!instance || !instance->incoming_queue) {
         fprintf(stderr,"Processor Thread (Inst %d): Invalid arguments or instance not properly initialized.\n", instance ? instance->id : -1);
         return NULL;
     }

    printf("SVM Processor thread started for instance %d.\n", instance->id);
    QueuedMessage processing_q_msg; // Структура для извлеченного сообщения + ID

    while (true) {
        // Пытаемся извлечь сообщение из ВХОДЯЩЕЙ очереди ЭТОГО экземпляра
        if (!qmq_dequeue(instance->incoming_queue, &processing_q_msg)) {
            // Очередь пуста и закрыта (Receiver завершился или shutdown из main)
            // Проверяем глобальный флаг или активность экземпляра (хотя is_active может быть неактуально, если main вызвал shutdown)
            if (!global_timer_keep_running || instance->incoming_queue->shutdown) {
                 printf("Processor Thread (Inst %d): Incoming queue empty and shutdown. Exiting.\n", instance->id);
                 break; // Корректный выход
            }
            // В противном случае это может быть ложное пробуждение, просто продолжаем
            usleep(10000);
            continue;
        }

        // Сообщение успешно извлечено
        // Проверяем ID на всякий случай (хотя читаем из очереди экземпляра)
        if (processing_q_msg.instance_id != instance->id) {
            fprintf(stderr, "Processor Thread (Inst %d): FATAL: Mismatched instance ID %d in instance queue.\n", instance->id, processing_q_msg.instance_id);
            break; // Ошибка логики
        }

        MessageHandler handler = message_handlers[processing_q_msg.message.header.message_type];
        Message* responseMessagePtr = NULL; // Указатель на ответное сообщение (malloc'нутое)

        if (handler != NULL) {
            // Вызываем обработчик, передавая ему указатель на ЭКЗЕМПЛЯР и СООБЩЕНИЕ
            responseMessagePtr = handler(instance, &processing_q_msg.message);
        } else {
            printf("Processor Thread (Inst %d): Unknown message type: %u (number %u)\n",
                   instance->id,
                   processing_q_msg.message.header.message_type,
                   get_full_message_number(&processing_q_msg.message.header));
        }

        // Если обработчик вернул ответное сообщение
        if (responseMessagePtr != NULL) {
            // Создаем QueuedMessage для ответа
            QueuedMessage response_q_msg;
            response_q_msg.instance_id = instance->id; // Сохраняем ID экземпляра-отправителя

            // Копируем данные из responseMessagePtr в response_q_msg.message
            // Проверяем размер перед копированием на всякий случай
            uint16_t resp_body_len_host = ntohs(responseMessagePtr->header.body_length);
            size_t resp_message_size = sizeof(MessageHeader) + resp_body_len_host;
             if (resp_message_size > sizeof(Message)) {
                 fprintf(stderr,"Processor Thread (Inst %d): ERROR: Response message size %zu exceeds buffer size %zu\n",
                         instance->id, resp_message_size, sizeof(Message));
                 // Можно либо обрезать, либо не отправлять. Пока копируем как есть.
                  memcpy(&response_q_msg.message, responseMessagePtr, sizeof(Message));
             } else {
                  memcpy(&response_q_msg.message, responseMessagePtr, resp_message_size);
                  // Опционально: обнулить остаток тела сообщения в response_q_msg.message.body
                  // memset(response_q_msg.message.body + resp_body_len_host, 0, MAX_MESSAGE_BODY_SIZE - resp_body_len_host);
             }
             // Убедимся, что заголовок скопирован корректно
             response_q_msg.message.header = responseMessagePtr->header;


            free(responseMessagePtr); // Освобождаем память, выделенную в handler'е

            // Помещаем ответ в ОБЩУЮ ИСХОДЯЩУЮ очередь
            if (!qmq_enqueue(svm_outgoing_queue, &response_q_msg)) {
                fprintf(stderr, "Processor Thread (Inst %d): Failed to enqueue response (type %u) to global outgoing queue.\n",
                       instance->id, response_q_msg.message.header.message_type);
                // Если не удалось поместить в исходящую очередь, она может быть закрыта (глобальное завершение)
                if (global_timer_keep_running) {
                    // Это неожиданно, если мы еще работаем. Сигнализируем о завершении?
                    // Пока просто выходим из потока экземпляра.
                     fprintf(stderr, "Processor Thread (Inst %d): Assuming global shutdown. Exiting.\n", instance->id);
                }
                break; // Выходим из цикла
            }
             //else {
             //    printf("Processor (Inst %d): Enqueued response type %u to outgoing queue\n", instance->id, response_q_msg.message.header.message_type);
             //}
        }
        // Если responseMessagePtr == NULL, ничего не делаем
     } // end while

     printf("SVM Processor thread finished for instance %d.\n", instance->id);
     // НЕ закрываем здесь общую исходящую очередь, это делает main
     return NULL;
}