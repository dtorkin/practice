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
#include "svm_timers.h" // Для global_timer_keep_running (как внешний флаг)
#include "svm_types.h"  // Для SvmInstance, QueuedMessage
#include <stdbool.h>

// Внешние переменные (глобальный флаг остановки)
extern volatile bool global_timer_keep_running; // Используем как основной флаг keep_running
extern pthread_mutex_t svm_instances_mutex; // Для изменения is_active

void* receiver_thread_func(void* arg) {
    SvmInstance *instance = (SvmInstance*)arg;
    if (!instance || instance->client_handle < 0 || !instance->io_handle || !instance->incoming_queue) {
         fprintf(stderr,"Receiver Thread (Inst %d): Invalid arguments or instance not properly initialized.\n", instance ? instance->id : -1);
         return NULL;
    }

    printf("SVM Receiver thread started for instance %d (handle: %d).\n", instance->id, instance->client_handle);
    Message receivedMessage; // Буфер для сообщения от io_common
    QueuedMessage q_msg;     // Структура для помещения в очередь
    q_msg.instance_id = instance->id;
    bool should_stop_instance = false; // Локальный флаг для выхода из цикла этого потока

    while (global_timer_keep_running && instance->is_active) { // Проверяем глобальный флаг и активность экземпляра
        // Используем io_handle и client_handle из структуры экземпляра
        int recvStatus = receive_protocol_message(instance->io_handle, instance->client_handle, &receivedMessage);

        if (recvStatus == -1) { // Ошибка чтения
            if(global_timer_keep_running && instance->is_active) { // Логируем только если еще работаем
                perror("Receiver Thread (Inst %d): receive_protocol_message error");
                fprintf(stderr, "Receiver Thread (Inst %d): Stopping instance due to receive error.\n", instance->id);
            }
            should_stop_instance = true;
        } else if (recvStatus == 1) { // Соединение закрыто удаленно
            if(global_timer_keep_running && instance->is_active) {
               printf("Receiver Thread (Inst %d): Connection closed by UVM. Stopping instance.\n", instance->id);
            }
            should_stop_instance = true;
        } else if (recvStatus == -2) { // Таймаут или прерывание (EINTR)
             // Проверяем флаги еще раз, если EINTR был из-за сигнала завершения
             if (!global_timer_keep_running || !instance->is_active) {
                 should_stop_instance = true;
             } else {
                 usleep(10000); // Небольшая пауза перед повторной попыткой
                 continue;
             }
        } else { // recvStatus == 0 - Успешное получение
            // Копируем полученное сообщение в структуру для очереди
            memcpy(&q_msg.message, &receivedMessage, sizeof(Message)); // Простое копирование

            // Помещаем сообщение во ВХОДЯЩУЮ очередь ЭТОГО экземпляра
            if (!queue_enqueue(instance->incoming_queue, &q_msg)) {
                 if (global_timer_keep_running && instance->is_active) {
                    fprintf(stderr, "Receiver Thread (Inst %d): Failed to enqueue message to instance incoming queue (maybe shutdown?). Stopping instance.\n", instance->id);
                 }
                 should_stop_instance = true; // Считаем ошибкой и завершаемся
            }
             //else {
             //    printf("Receiver (Inst %d): Enqueued msg type %u\n", instance->id, q_msg.message.header.message_type);
             //}
        }

        // Если нужно остановить этот экземпляр
        if(should_stop_instance) {
            // Помечаем экземпляр как неактивный (под глобальным мьютексом)
            pthread_mutex_lock(&svm_instances_mutex);
            if (instance->is_active) { // Доп. проверка, если другой поток уже остановил
                instance->is_active = false;
                printf("Receiver Thread (Inst %d): Marked instance as inactive.\n", instance->id);
                // Закрываем хэндл здесь не нужно, это сделает main при очистке или sender при ошибке отправки
            }
            pthread_mutex_unlock(&svm_instances_mutex);

            // Сигнализируем процессору этого экземпляра, что новых сообщений не будет
            if (instance->incoming_queue) {
                printf("Receiver Thread (Inst %d): Shutting down incoming queue.\n", instance->id);
                queue_shutdown(instance->incoming_queue);
            }
            break; // Выходим из while
        }
	} // end while

    printf("SVM Receiver thread finished for instance %d.\n", instance->id);
    return NULL;
}