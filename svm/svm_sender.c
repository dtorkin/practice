/*
 * svm/svm_sender.c
 *
 * Описание:
 * Реализация ОБЩЕГО потока-отправителя.
 * Читает сообщения (QueuedMessage) из общей исходящей очереди,
 * находит нужный экземпляр по ID и отправляет сообщение клиенту.
 * Обрабатывает ошибки отправки и деактивирует экземпляр при необходимости.
 */
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/socket.h> // Для shutdown, SHUT_RDWR
#include <errno.h>
#include "../io/io_common.h"
#include "../utils/ts_queue.h"
#include "../utils/ts_queued_msg_queue.h"
#include "svm_timers.h" // Для global_timer_keep_running
#include "svm_types.h"  // Для SvmInstance, QueuedMessage

// Внешние переменные
extern ThreadSafeQueuedMsgQueue *svm_outgoing_queue; // Общая исходящая очередь
extern SvmInstance svm_instances[MAX_SVM_INSTANCES]; // Массив экземпляров
extern ThreadSafeQueuedMsgQueue *svm_outgoing_queue = NULL;   // Общая исходящая очередь QueuedMessage
extern volatile bool global_timer_keep_running; // Глобальный флаг остановки
extern pthread_mutex_t svm_instances_mutex; // Мьютекс для доступа к массиву экземпляров


void* sender_thread_func(void* arg) {
    (void)arg;
    printf("SVM Sender thread started (reads global outgoing queue).\n");
    QueuedMessage queuedMsgToSend; // Буфер для извлеченного сообщения + ID

    while(true) {
        // Пытаемся извлечь сообщение из ОБЩЕЙ ИСХОДЯЩЕЙ очереди
        if (!qmq_dequeue(svm_outgoing_queue, &queuedMsgToSend)) {
            // Очередь пуста и закрыта, или ошибка
            if (!global_timer_keep_running && svm_outgoing_queue->count == 0) {
                 printf("Sender Thread: Outgoing queue empty and shutdown signaled. Exiting.\n");
                 break; // Корректный выход
            }
             if(global_timer_keep_running) {
                 // Возможно, ложное пробуждение или ошибка
                 usleep(10000);
                 continue;
             } else {
                 // Очередь закрыта, но может содержать элементы
                 if (svm_outgoing_queue->count == 0) {
                    break; // Выход, если закрыта и пуста
                 } else {
                    continue; // Продолжаем обрабатывать остатки
                 }
             }
        }

        // Сообщение успешно извлечено
        int instance_id = queuedMsgToSend.instance_id;

        // Находим нужный экземпляр и его хэндл (под мьютексом)
        int client_handle = -1;
        IOInterface *io_handle = NULL;
        bool instance_was_active = false;

        pthread_mutex_lock(&svm_instances_mutex);
        if (instance_id >= 0 && instance_id < MAX_SVM_INSTANCES && svm_instances[instance_id].is_active) {
            instance_was_active = true;
            client_handle = svm_instances[instance_id].client_handle;
            io_handle = svm_instances[instance_id].io_handle;
        } else {
             // Если экземпляр уже не активен, просто игнорируем сообщение для него
             // printf("Sender Thread: Instance %d is not active. Discarding message type %u.\n",
             //        instance_id, queuedMsgToSend.message.header.message_type);
        }
        pthread_mutex_unlock(&svm_instances_mutex);

        // Если экземпляр был активен и мы получили его хэндлы
        if (instance_was_active && client_handle >= 0 && io_handle != NULL) {
            // printf("Sender Thread: Sending msg type %u to instance %d (handle %d)\n",
            //        queuedMsgToSend.message.header.message_type, instance_id, client_handle);

            // Отправляем сообщение
            if (send_protocol_message(io_handle, client_handle, &queuedMsgToSend.message) != 0) {
                // Ошибка отправки (клиент мог отключиться между проверкой и отправкой)
                fprintf(stderr, "Sender Thread: Error sending message (type %u) to instance %d (handle %d). Deactivating instance.\n",
                       queuedMsgToSend.message.header.message_type, instance_id, client_handle);

                // Помечаем экземпляр как неактивный и закрываем его ресурсы (под мьютексом)
                pthread_mutex_lock(&svm_instances_mutex);
                 if (instance_id >= 0 && instance_id < MAX_SVM_INSTANCES && svm_instances[instance_id].is_active) {
                     // Используем shutdown для попытки разбудить Receiver
                     if (svm_instances[instance_id].client_handle >= 0) {
                        shutdown(svm_instances[instance_id].client_handle, SHUT_RDWR);
                     }
                     // Закрываем соединение через интерфейс
                     if (svm_instances[instance_id].io_handle && svm_instances[instance_id].client_handle >= 0) {
                         svm_instances[instance_id].io_handle->disconnect(svm_instances[instance_id].io_handle, svm_instances[instance_id].client_handle);
                     }
                     svm_instances[instance_id].client_handle = -1;
                     svm_instances[instance_id].is_active = false;
                     printf("Sender Thread: Instance %d deactivated due to send error.\n", instance_id);

                     // Закрываем входящую очередь этого экземпляра, чтобы разбудить его процессор
                     if (svm_instances[instance_id].incoming_queue) {
                          qmq_shutdown(svm_instances[instance_id].incoming_queue);
                     }
                     // Можно также отменить потоки Receiver/Processor, но лучше дать им завершиться штатно
                 }
                pthread_mutex_unlock(&svm_instances_mutex);
                // Не останавливаем весь Sender, продолжаем обрабатывать другие сообщения
            }
        }
    } // end while

    printf("SVM Sender thread finished.\n");
    return NULL;
}