/*
 * svm/svm_receiver.c
 *
 * Описание:
 * Реализация потока-приемника для ОДНОГО экземпляра SVM.
 * Читает сообщения из сети и помещает их во входящую очередь экземпляра.
 * ВРЕМЕННАЯ РЕАЛИЗАЦИЯ: 1 поток на 1 экземпляр.
 */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h> // Для memcpy
#include <errno.h>
#include "../io/io_common.h"
#include "../utils/ts_queue.h"
#include "../utils/ts_queued_msg_queue.h"
#include "svm_timers.h" // Для global_timer_keep_running (как внешний флаг)
#include "svm_types.h"  // Для SvmInstance, QueuedMessage
#include <stdbool.h>

// Внешняя переменная (общий флаг работы svm_app)
// extern volatile bool global_timer_keep_running; // Если вы переименовали в svm_main.c
extern volatile bool keep_running; // Используем имя из svm_main.c

// extern pthread_mutex_t svm_instances_mutex; // Это было для глобального мьютекса,
                                             // который мы решили не использовать для is_active

void* receiver_thread_func(void* arg) {
    SvmInstance *instance = (SvmInstance*)arg;
    if (!instance || instance->client_handle < 0 || !instance->io_handle || !instance->incoming_queue) {
         fprintf(stderr,"Receiver Thread (Inst %d): Invalid arguments or instance not properly initialized.\n", instance ? instance->id : -1);
         return NULL;
    }

    printf("SVM Receiver thread started for instance %d (LAK 0x%02X, handle: %d).\n",
           instance->id, instance->assigned_lak, instance->client_handle);
    Message receivedMessage;
    QueuedMessage q_msg;
    q_msg.instance_id = instance->id;
    // bool should_stop_instance_locally = false; // Переименуем для ясности

    // Используем instance->is_active (который управляется listener'ом)
    // и глобальный keep_running
    while (keep_running && instance->is_active) {
        int recvStatus = receive_protocol_message(instance->io_handle, instance->client_handle, &receivedMessage);

        // Проверяем состояние instance->is_active СРАЗУ после блокирующего вызова,
        // так как listener мог изменить его во время нашего ожидания на recv.
        // Это предотвратит обработку сообщения, если экземпляр уже деактивирован.
        // Однако, instance_mutex здесь брать не очень хорошо, т.к. listener его держит при деактивации.
        // Лучше положиться на то, что listener закроет сокет, и recvStatus будет != 0.

        if (!keep_running || !instance->is_active) { // Проверяем флаги еще раз
            // printf("Receiver Thread (Inst %d): Shutdown signaled or instance deactivated during/after receive. Exiting.\n", instance->id);
            break;
        }

        if (recvStatus == 0) { // Успешное получение сообщения
            memcpy(&q_msg.message, &receivedMessage, sizeof(Message));
            if (!qmq_enqueue(instance->incoming_queue, &q_msg)) {
                 if (keep_running && instance->is_active) { // Логируем, только если еще должны работать
                    fprintf(stderr, "Receiver Thread (Inst %d): Failed to enqueue message to instance incoming queue. Stopping instance.\n", instance->id);
                 }
                 // Listener должен будет сам закрыть сокет и пометить is_active = false
                 // Здесь мы просто завершаем поток, сигнализируя процессору.
                 break; 
            }
        } else if (recvStatus == 1) { // Соединение закрыто удаленно
            if(keep_running && instance->is_active) {
               printf("Receiver Thread (Inst %d): Connection closed by UVM. Stopping instance processing.\n", instance->id);
            }
            break; 
        } else if (recvStatus == -1) { // Ошибка чтения (не таймаут/EINTR)
            if(keep_running && instance->is_active) {
                // perror("Receiver Thread (Inst %d): receive_protocol_message error");
                fprintf(stderr, "Receiver Thread (Inst %d): Receive error %d (%s). Stopping instance processing.\n", instance->id, errno, strerror(errno));
            }
            break; 
        } else if (recvStatus == -2) { // Таймаут или EINTR из receive_protocol_message
             // EINTR уже должен обрабатываться внутри receive_protocol_message,
             // но если он просачивается, или это наш кастомный таймаут poll'а.
             // Просто продолжаем цикл, чтобы снова проверить keep_running и is_active.
             usleep(10000); // Небольшая пауза, чтобы не забивать CPU
             continue;
        }
	} // end while

    // Когда выходим из цикла, нужно сигнализировать процессору, что новых сообщений не будет.
    // Это делается путем закрытия входящей очереди экземпляра.
    // Также нужно сообщить listener'у, что экземпляр больше не активен, чтобы он мог очистить ресурсы.
    // Это лучше делать в listener'е, который ждет этот поток.
    // Здесь мы просто сигнализируем процессору.

    printf("SVM Receiver thread (Inst %d, LAK 0x%02X): Shutting down incoming queue and finishing.\n", instance->id, instance->assigned_lak);
    if (instance->incoming_queue) {
        qmq_shutdown(instance->incoming_queue); // Сигнализируем процессору
    }

    // Listener должен будет изменить instance->is_active = false;
    // pthread_mutex_lock(&instance->instance_mutex); // Неправильно, это может привести к дедлоку с listener
    // instance->is_active = false;
    // pthread_mutex_unlock(&instance->instance_mutex);

    printf("SVM Receiver thread finished for instance %d (LAK 0x%02X).\n", instance->id, instance->assigned_lak);
    return NULL;
}