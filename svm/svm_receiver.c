/*
 * svm/svm_receiver.c
 */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "../io/io_common.h"
#include "../utils/ts_queue.h"
#include "svm_timers.h" // Для keep_running
#include <stdbool.h>    // Для bool

// Внешние переменные из svm_main.c
extern IOInterface *io_svm;
extern int global_client_handle;
extern ThreadSafeQueue *svm_incoming_queue;
extern ThreadSafeQueue *svm_outgoing_queue; // Нужен для shutdown
extern volatile bool keep_running;

void* receiver_thread_func(void* arg) {
    (void)arg;
    printf("SVM Receiver thread started (handle: %d).\n", global_client_handle);
    Message receivedMessage;
    bool should_stop = false; // Локальный флаг для выхода из цикла

    while (keep_running) { // Основной цикл зависит от глобального флага
        int recvStatus = receive_protocol_message(io_svm, global_client_handle, &receivedMessage);

        if (recvStatus == -1) {
            if(keep_running) fprintf(stderr, "Receiver Thread: Ошибка получения сообщения. Инициировано завершение.\n");
            should_stop = true;
        } else if (recvStatus == 1) {
            printf("Receiver Thread: Соединение закрыто UVM. Инициировано завершение.\n");
            should_stop = true;
        } else if (recvStatus == -2) {
             usleep(10000); // Таймаут/EINTR - просто ждем еще
             continue;
        } else { // recvStatus == 0 - Успешное получение
            // Помещаем сообщение в очередь
            if (!queue_enqueue(svm_incoming_queue, &receivedMessage)) {
                 if (keep_running) { // Если не штатное завершение
                    fprintf(stderr, "Receiver Thread: Не удалось добавить сообщение во входящую очередь (закрыта?). Инициировано завершение.\n");
                 }
                 should_stop = true; // Считаем ошибкой и завершаемся
            }
        }

        // Если нужно остановиться, выходим из цикла
        if(should_stop) {
            if (keep_running) { // Только если мы первые инициируем остановку
                keep_running = false; // Устанавливаем глобальный флаг
                // Будим другие потоки, которые могут ждать
                queue_shutdown(svm_incoming_queue);
                queue_shutdown(svm_outgoing_queue);
            }
            break; // Выходим из while
        }
	}

    printf("SVM Receiver thread finished.\n");
    return NULL;
}