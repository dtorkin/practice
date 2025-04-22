/*
 * svm/svm_sender.c
 * Описание: ОБЩИЙ поток-отправитель.
 * Читает QueuedMessage из общей очереди, находит экземпляр,
 * отправляет, имитирует отключение по счетчику.
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <errno.h>
#include "../io/io_common.h"
#include "../utils/ts_queued_msg_queue.h"
#include "svm_timers.h"
#include "svm_types.h"

// Внешние глобальные переменные
extern ThreadSafeQueuedMsgQueue *svm_outgoing_queue;
extern SvmInstance svm_instances[MAX_SVM_INSTANCES];
extern volatile bool keep_running;
extern pthread_mutex_t svm_instances_mutex;

void* sender_thread_func(void* arg) {
    (void)arg;
    printf("SVM Sender thread started (reads global outgoing queue).\n");
    QueuedMessage queuedMsgToSend;

    while(true) {
        if (!qmq_dequeue(svm_outgoing_queue, &queuedMsgToSend)) {
            if (!keep_running && svm_outgoing_queue->count == 0) { break; }
            if (keep_running) usleep(10000);
            continue;
        }

        int instance_id = queuedMsgToSend.instance_id;
        SvmInstance *instance = NULL;
        int client_handle = -1;
        IOInterface *io_handle = NULL;
        bool instance_is_active = false;
        bool limit_reached_this_time = false;

        if (instance_id < 0 || instance_id >= MAX_SVM_INSTANCES) {
             fprintf(stderr,"Sender Thread: Invalid instance ID %d in outgoing queue.\n", instance_id);
             continue;
        }
        instance = &svm_instances[instance_id];

        // --- Проверяем статус и счетчик ДО отправки ---
        pthread_mutex_lock(&instance->instance_mutex);
        instance_is_active = instance->is_active;
        if (instance_is_active) {
            client_handle = instance->client_handle;
            io_handle = instance->io_handle;
            int disconnect_threshold = instance->disconnect_after_messages;

            if (disconnect_threshold > 0) {
                 // Проверяем, НЕ достигли ли мы лимита НА ПРЕДЫДУЩЕМ сообщении
                 if (instance->messages_sent_count < disconnect_threshold) {
                      // Увеличиваем счетчик ПЕРЕД отправкой ТЕКУЩЕГО
                      instance->messages_sent_count++;
                      // Проверяем, НЕ достиг ли лимит ИМЕННО СЕЙЧАС
                      if (instance->messages_sent_count >= disconnect_threshold) {
                           limit_reached_this_time = true;
                           printf("Sender Thread: Instance %d reached message limit (%d >= %d). Will disconnect AFTER this send.\n",
                                  instance_id, instance->messages_sent_count, disconnect_threshold);
                           // НЕ помечаем is_active=false здесь, сделаем после отправки
                           // Но можем закрыть очередь процессора
                           // if (instance->incoming_queue) qmq_shutdown(instance->incoming_queue); // Рано?
                      }
                 } else {
                      // Лимит уже был достигнут ранее, отправлять не должны
                      instance_is_active = false;
                      printf("Sender Thread: Instance %d message limit %d already reached. Discarding msg type %u.\n",
                             instance_id, disconnect_threshold, queuedMsgToSend.message.header.message_type);
                 }
            }
        }
        pthread_mutex_unlock(&instance->instance_mutex);
        // --- Конец проверки статуса и счетчика ---

        // Отправляем, только если все еще активны и хэндлы валидны
        bool send_error = false;
        if (instance_is_active && client_handle >= 0 && io_handle != NULL) {
             // printf("Sender Thread: Sending msg type %u to instance %d (handle %d), sent count %d\n", ...);
            if (send_protocol_message(io_handle, client_handle, &queuedMsgToSend.message) != 0) {
                send_error = true; // Ошибка отправки
                if (keep_running) {
                     fprintf(stderr, "Sender Thread: Error sending message (type %u) to instance %d (handle %d).\n",
                            queuedMsgToSend.message.header.message_type, instance_id, client_handle);
                }
            }
        } else if (instance_is_active) { // Был активен, но хэндлы невалидны?
             fprintf(stderr,"Sender Thread: Instance %d active but handles invalid? Discarding msg type %u.\n",
                     instance_id, queuedMsgToSend.message.header.message_type);
             send_error = true; // Считаем ошибкой
        }
        // Если !instance_is_active, сообщение просто игнорируется

		// --- Обработка после попытки отправки ---
        if (send_error || limit_reached_this_time) {
             pthread_mutex_lock(&instance->instance_mutex); // Берем мьютекс для изменения состояния instance
             if (instance->is_active) { // Проверяем снова, активен ли он еще

                  // Сначала логируем причину деактивации
                  if (limit_reached_this_time) {
                       // ---> Сообщение об имитации отключения <---
                       fprintf(stderr, "Sender Thread: SIMULATING disconnect for instance %d (handle %d) NOW after sending message %d.\n",
                              instance_id, instance->client_handle, instance->messages_sent_count);
                  } else { // send_error == true
                       fprintf(stderr, "Sender Thread: Deactivating instance %d (handle %d) due to send error.\n",
                               instance_id, instance->client_handle);
                  }

                  // Теперь деактивируем и закрываем ресурсы
                  instance->is_active = false; // Помечаем неактивным

                  // Закрываем сокет, чтобы Receiver узнал
                  if (instance->client_handle >= 0) {
                      shutdown(instance->client_handle, SHUT_RDWR);
                      // close() будет вызван в listener'е при очистке
                  }
                  // Закрываем входящую очередь, чтобы Processor завершился
                  if (instance->incoming_queue) {
                       qmq_shutdown(instance->incoming_queue);
                  }
             }
             // Если !instance->is_active, значит его уже деактивировали (возможно, Receiver или другой вызов Sender'а)
             pthread_mutex_unlock(&instance->instance_mutex); // Отпускаем мьютекс
        }
        // --- Конец обработки ---

    } // end while

    printf("SVM Sender thread finished.\n");
    return NULL;
}