/*
 * uvm/uvm_receiver.c
 * Описание: Поток UVM для приема сообщений от ОДНОГО SVM.
 * Запускается по одному на каждое активное соединение.
 */
#include "uvm_receiver.h" // (Если есть)
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h> // Для memcpy

#include "../io/io_common.h"
#include "../utils/ts_uvm_resp_queue.h" // Новая очередь ответов
#include "uvm_types.h"
#include "../config/config.h" // Для MAX_SVM_INSTANCES

// Внешние переменные из uvm_main.c
extern ThreadSafeUvmRespQueue *uvm_incoming_response_queue; // Общая очередь ответов
extern UvmSvmLink svm_links[MAX_SVM_INSTANCES]; // Нужен для обновления статуса
extern pthread_mutex_t uvm_links_mutex;      // Мьютекс для доступа к svm_links
extern volatile bool uvm_keep_running;

void* uvm_receiver_thread_func(void* arg) {
    UvmSvmLink *link = (UvmSvmLink*)arg;
    if (!link || !link->io_handle || link->connection_handle < 0 || link->id < 0 || link->id >= MAX_SVM_INSTANCES) {
        fprintf(stderr, "Receiver Thread (SVM ?): Invalid arguments provided.\n");
        return NULL;
    }

    int svm_id = link->id; // Сохраняем ID для логов
    int handle = link->connection_handle;
    IOInterface* io = link->io_handle;

    printf("UVM Receiver thread started for SVM ID %d (handle: %d).\n", svm_id, handle);
    Message receivedMessage;
    UvmResponseMessage response_msg; // Структура для очереди
    response_msg.source_svm_id = svm_id;
    bool should_stop_thread = false;

    while (uvm_keep_running) {
        // Проверяем статус соединения перед чтением (на случай если Sender пометил FAILED)
        pthread_mutex_lock(&uvm_links_mutex);
        bool is_active = (link->status == UVM_LINK_ACTIVE);
        pthread_mutex_unlock(&uvm_links_mutex);

        if (!is_active) {
             printf("Receiver Thread (SVM %d): Link is no longer active. Exiting.\n", svm_id);
             should_stop_thread = true;
             break;
        }

        // Читаем сообщение
        int recvStatus = receive_protocol_message(io, handle, &receivedMessage);

        if (recvStatus == -1) { // Ошибка чтения
            if(uvm_keep_running) {
                perror("Receiver Thread (SVM %d): receive_protocol_message error");
                fprintf(stderr, "Receiver Thread (SVM %d): Marking link as FAILED due to receive error.\n", svm_id);
            }
            should_stop_thread = true;
		} else if (recvStatus == 1) { // Соединение закрыто удаленно
			 if(uvm_keep_running) {
				printf("Receiver Thread (SVM %d): Connection closed by SVM. Marking link as INACTIVE.\n", svm_id);
			 }
			should_stop_thread = true;
			// --- УСТАНОВКА СТАТУСА И ОТПРАВКА СОБЫТИЯ В GUI ---
			pthread_mutex_lock(&uvm_links_mutex);
			if (link->status == UVM_LINK_ACTIVE) { // Проверяем, чтобы не изменить FAILED на INACTIVE
				link->status = UVM_LINK_INACTIVE; // <-- Статус обновляется
				// Формируем и отправляем событие для GUI
				char gui_event_buffer[128];
				snprintf(gui_event_buffer, sizeof(gui_event_buffer),
						 "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X",
						 svm_id, UVM_LINK_INACTIVE, link->assigned_lak);
				send_to_gui_socket(gui_event_buffer); // <-- Отправляем событие
			}
			pthread_mutex_unlock(&uvm_links_mutex);
			break; // Выходим штатно
        } else if (recvStatus == -2) { // Таймаут/EINTR
             if (!uvm_keep_running) { // Прервано из-за сигнала завершения
                  should_stop_thread = true;
             } else {
                  usleep(10000);
                  continue;
             }
        } else { // Успех
			pthread_mutex_lock(&uvm_links_mutex); // Захватываем мьютекс
			link->last_activity_time = time(NULL); // Обновляем время активности
			pthread_mutex_unlock(&uvm_links_mutex); // Отпускаем мьютекс
            // Копируем сообщение в структуру для очереди
			response_msg.source_svm_id = svm_id; // <-- Устанавливаем ID ПЕРЕД enqueue
			memcpy(&response_msg.message, &receivedMessage, sizeof(Message)); // Копируем сообщение
			// Помещаем в ОБЩУЮ очередь ответов
			if (!uvq_enqueue(uvm_incoming_response_queue, &response_msg)) {
                 if (uvm_keep_running) {
                    fprintf(stderr, "Receiver Thread (SVM %d): Failed to enqueue message to response queue (shutdown?). Exiting.\n", svm_id);
                 }
                 should_stop_thread = true; // Не смогли добавить, выходим
            }
             //else {
             //    printf("Receiver (SVM %d): Enqueued response type %u\n", svm_id, response_msg.message.header.message_type);
             //}
        }

        // Если нужно остановиться (ошибка или сигнал)
        if(should_stop_thread) {
            // Помечаем линк как FAILED (если еще не помечен как INACTIVE)
            pthread_mutex_lock(&uvm_links_mutex);
            if (link->status == UVM_LINK_ACTIVE) {
                link->status = UVM_LINK_FAILED;
                printf("Receiver Thread (SVM %d): Marked link as FAILED.\n", svm_id);
                 // Можно закрыть хэндл здесь, чтобы Sender тоже заметил
                 // if (link->connection_handle >= 0) {
                 //     shutdown(link->connection_handle, SHUT_RDWR);
                 // }
            }
            pthread_mutex_unlock(&uvm_links_mutex);
            break; // Выходим из while
        }
    } // end while

    printf("UVM Receiver thread for SVM ID %d finished.\n", svm_id);
    return NULL;
}