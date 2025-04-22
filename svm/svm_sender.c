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
extern SvmInstance svm_instances[MAX_SVM_CONFIGS];
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
        SvmInstance *instance = NULL; // Указатель на экземпляр

        // --- Получаем данные экземпляра и проверяем отключение ---
        int client_handle = -1;
        IOInterface *io_handle = NULL;
        bool should_disconnect = false;
        bool instance_is_active = false;
        int current_sent_count = 0;
        int disconnect_threshold = -1;

        if (instance_id >= 0 && instance_id < MAX_SVM_CONFIGS) {
            instance = &svm_instances[instance_id];
            pthread_mutex_lock(&instance->instance_mutex); // Блокируем экземпляр

            instance_is_active = instance->is_active;
            if (instance_is_active) {
                client_handle = instance->client_handle;
                io_handle = instance->io_handle;
                disconnect_threshold = instance->disconnect_after_messages;

                // Инкрементируем счетчик ПЕРЕД отправкой (чтобы N-е сообщение было последним)
                if (disconnect_threshold > 0) {
                     instance->messages_sent_count++;
                     current_sent_count = instance->messages_sent_count; // Сохраняем для лога
                     if (current_sent_count >= disconnect_threshold) {
                         should_disconnect = true;
                         instance->is_active = false; // Помечаем неактивным СРАЗУ
                         printf("Sender Thread: Instance %d reached message limit (%d >= %d). Preparing disconnect.\n",
                                instance_id, current_sent_count, disconnect_threshold);
                         // Закрываем очередь, чтобы разбудить процессор
                         if (instance->incoming_queue) qmq_shutdown(instance->incoming_queue);
                     }
                } else {
                     current_sent_count = instance->messages_sent_count; // Все равно сохраняем для лога
                }
            }
            pthread_mutex_unlock(&instance->instance_mutex); // Отпускаем мьютекс экземпляра
        } else {
             fprintf(stderr,"Sender Thread: Invalid instance ID %d in outgoing queue.\n", instance_id);
             continue; // Пропускаем это сообщение
        }
        // --- Конец блока получения данных ---


        // Отправляем, только если активен и хэндлы валидны
        if (instance_is_active && client_handle >= 0 && io_handle != NULL) {
             // Отладочный лог перед отправкой
             // printf("Sender Thread: Sending msg type %u to instance %d (handle %d), sent count %d\n",
             //       queuedMsgToSend.message.header.message_type, instance_id, client_handle, current_sent_count);

            if (send_protocol_message(io_handle, client_handle, &queuedMsgToSend.message) != 0) {
                // Ошибка отправки
                if (keep_running) { // Логируем ошибку, только если не штатное завершение
                     fprintf(stderr, "Sender Thread: Error sending message (type %u) to instance %d (handle %d). Deactivating instance.\n",
                            queuedMsgToSend.message.header.message_type, instance_id, client_handle);
                }
                // Помечаем как неактивный и закрываем ресурсы
                pthread_mutex_lock(&instance->instance_mutex);
                 if (instance->is_active) { // Доп. проверка
                     if (instance->client_handle >= 0) {
                         shutdown(instance->client_handle, SHUT_RDWR);
                     }
                     instance->is_active = false;
                     if (instance->incoming_queue) {
                          qmq_shutdown(instance->incoming_queue);
                     }
                 }
                pthread_mutex_unlock(&instance->instance_mutex);
            } else if (should_disconnect) {
                 // Успешно отправили ПОСЛЕДНЕЕ сообщение, теперь отключаем
                 printf("Sender Thread: SIMULATING disconnect for instance %d after sending message %d.\n", instance_id, current_sent_count);
                 pthread_mutex_lock(&instance->instance_mutex);
                 if(instance->client_handle >= 0) { // Хендл все еще должен быть валиден
                      shutdown(instance->client_handle, SHUT_RDWR); // Разрываем соединение
                      // close() будет вызван в listener'е
                 }
                 // is_active уже false
                 pthread_mutex_unlock(&instance->instance_mutex);
            }
            // Если не было ошибки и не надо отключаться, просто успешно отправили
        } else if (!instance_is_active && client_handle >= 0){
             // Экземпляр стал неактивным (из-за should_disconnect или ошибки в другом потоке)
             printf("Sender Thread: Instance %d became inactive before send could complete. Message type %u discarded.\n",
                    instance_id, queuedMsgToSend.message.header.message_type);
        }
    } // end while

    printf("SVM Sender thread finished.\n");
    return NULL;
}