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
#include "../utils/ts_queued_msg_queue.h" // Используем новую очередь
#include "svm_timers.h" // Для global_timer_keep_running
#include "svm_types.h"  // Для SvmInstance, QueuedMessage

// Внешние глобальные переменные
extern ThreadSafeQueuedMsgQueue *svm_outgoing_queue;
extern SvmInstance svm_instances[MAX_SVM_INSTANCES];
extern volatile bool keep_running; // Используем глобальный флаг
extern pthread_mutex_t svm_instances_mutex; // Нужен для безопасного доступа к is_active? (Хотя сейчас читаем только хэндл)

void* sender_thread_func(void* arg) {
    (void)arg;
    printf("SVM Sender thread started (reads global outgoing queue).\n");
    QueuedMessage queuedMsgToSend;

    while(true) {
        if (!qmq_dequeue(svm_outgoing_queue, &queuedMsgToSend)) {
            if (!keep_running && svm_outgoing_queue->count == 0) { /*...*/ break; }
            if (keep_running) usleep(10000);
            continue;
        }

        int instance_id = queuedMsgToSend.instance_id;

        // Получаем хэндл и IO ИЗ ЭКЗЕМПЛЯРА (под мьютексом экземпляра)
        int client_handle = -1;
        IOInterface *io_handle = NULL;
        bool should_disconnect = false;
        bool instance_is_active = false; // Проверяем активность перед отправкой

        if (instance_id >= 0 && instance_id < MAX_SVM_INSTANCES) {
            SvmInstance *instance = &svm_instances[instance_id];
            pthread_mutex_lock(&instance->instance_mutex); // Блокируем экземпляр

            instance_is_active = instance->is_active; // Проверяем активность
            if (instance_is_active) {
                client_handle = instance->client_handle;
                io_handle = instance->io_handle; // Получаем IO из instance
                instance->messages_sent_count++; // Увеличиваем счетчик отправленных

                // Проверяем, не пора ли отключаться
                if (instance->disconnect_after_messages > 0 &&
                    instance->messages_sent_count >= instance->disconnect_after_messages)
                {
                    should_disconnect = true;
                    instance->is_active = false; // Помечаем неактивным
                    printf("Sender Thread: SIMULATING disconnect for instance %d after %d messages.\n",
                           instance_id, instance->messages_sent_count);
                    // Закрываем очередь, чтобы разбудить процессор
                    if (instance->incoming_queue) qmq_shutdown(instance->incoming_queue);
                }
            }
            pthread_mutex_unlock(&instance->instance_mutex); // Отпускаем мьютекс экземпляра
        } else {
             fprintf(stderr,"Sender Thread: Invalid instance ID %d in outgoing queue.\n", instance_id);
        }

        // Отправляем, только если активен и хэндлы валидны
        if (instance_is_active && client_handle >= 0 && io_handle != NULL) {
             //printf("Sender Thread: Sending msg type %u to instance %d (handle %d), sent count %d\n",
             //       queuedMsgToSend.message.header.message_type, instance_id, client_handle, svm_instances[instance_id].messages_sent_count);

            if (send_protocol_message(io_handle, client_handle, &queuedMsgToSend.message) != 0) {
                // Ошибка отправки
                fprintf(stderr, "Sender Thread: Error sending message (type %u) to instance %d (handle %d). Deactivating instance.\n",
                       queuedMsgToSend.message.header.message_type, instance_id, client_handle);

                // Помечаем как неактивный и закрываем ресурсы (под мьютексом экземпляра)
                pthread_mutex_lock(&svm_instances[instance_id].instance_mutex);
                 if (svm_instances[instance_id].is_active) { // Доп. проверка
                     if (svm_instances[instance_id].client_handle >= 0) {
                         shutdown(svm_instances[instance_id].client_handle, SHUT_RDWR);
                         // disconnect будет вызван в listener'е
                     }
                     svm_instances[instance_id].is_active = false;
                     if (svm_instances[instance_id].incoming_queue) {
                          qmq_shutdown(svm_instances[instance_id].incoming_queue);
                     }
                 }
                pthread_mutex_unlock(&svm_instances[instance_id].instance_mutex);
                // Не останавливаем весь Sender
            } else if (should_disconnect) {
                 // Успешно отправили последнее сообщение, теперь отключаем
                 pthread_mutex_lock(&svm_instances[instance_id].instance_mutex);
                 if(svm_instances[instance_id].client_handle >= 0) {
                      shutdown(svm_instances[instance_id].client_handle, SHUT_RDWR);
                 }
                 // Статус is_active уже false
                 pthread_mutex_unlock(&svm_instances[instance_id].instance_mutex);
            }
        } else if (!instance_is_active && client_handle >= 0){
             // Экземпляр стал неактивным между проверкой и отправкой, или из-за should_disconnect
             printf("Sender Thread: Instance %d became inactive before/during send. Message type %u discarded.\n",
                    instance_id, queuedMsgToSend.message.header.message_type);
        }
    } // end while

    printf("SVM Sender thread finished.\n");
    return NULL;
}