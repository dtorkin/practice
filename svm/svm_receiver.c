/*
 * svm/svm_receiver.c
 * Описание: Поток-приемник для ОДНОГО SVM.
 * (Возвращено к одно-экземплярной модели)
 */
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/socket.h>
#include "../io/io_common.h"
#include "../utils/ts_queue.h" // Используем обычную очередь
#include "svm_timers.h" // Для timer_thread_running

// Внешние глобальные переменные из svm_main.c
extern IOInterface *io_svm;
extern int global_client_handle;
extern ThreadSafeQueue *svm_incoming_queue;
extern ThreadSafeQueue *svm_outgoing_queue; // Нужен для shutdown
extern volatile bool keep_running; // Глобальный флаг

void* receiver_thread_func(void* arg) {
    (void)arg;
    if (global_client_handle < 0) {
        fprintf(stderr,"Receiver Thread: Invalid client handle.\n");
        return NULL;
    }
    printf("SVM Receiver thread started (handle: %d).\n", global_client_handle);
    Message receivedMessage; // Буфер для Message
    bool should_stop = false;

    while (keep_running) {
        int recvStatus = receive_protocol_message(io_svm, global_client_handle, &receivedMessage);

        if (recvStatus == -1) { // Ошибка
            if(keep_running) fprintf(stderr, "Receiver Thread: Receive error. Initiating shutdown.\n");
            should_stop = true;
        } else if (recvStatus == 1) { // Закрыто удаленно
            if(keep_running) printf("Receiver Thread: Connection closed by UVM. Initiating shutdown.\n");
            should_stop = true;
        } else if (recvStatus == -2) { // Таймаут/EINTR
             if (!keep_running) { // Прервано из-за сигнала завершения
                  should_stop = true;
             } else {
                  usleep(10000);
                  continue;
             }
        } else { // Успех
            // Помещаем Message в очередь
            if (!queue_enqueue(svm_incoming_queue, &receivedMessage)) {
                 if (keep_running) {
                    fprintf(stderr, "Receiver Thread: Failed to enqueue message (queue shutdown?). Initiating shutdown.\n");
                 }
                 should_stop = true;
            }
        }

        if(should_stop) {
            if (keep_running) { // Только если мы первые инициируем
                keep_running = false;
                // Будим другие потоки
                if (svm_incoming_queue) queue_shutdown(svm_incoming_queue);
                if (svm_outgoing_queue) queue_shutdown(svm_outgoing_queue);
                stop_timer_thread(); // Останавливаем таймер
                // Закрываем сокет, чтобы разбудить accept в main (если он еще ждет)
                // или для надежности
                if (global_client_handle >= 0) {
                     shutdown(global_client_handle, SHUT_RDWR);
                     // close(global_client_handle); // Закроет main
                }
            }
            break; // Выходим из while
        }
    } // end while

    printf("SVM Receiver thread finished.\n");
    return NULL;
}