/*
 * uvm/uvm_utils.c
 *
 * Описание:
 * Вспомогательные функции для модуля UVM.
 */
#include "uvm_utils.h" // Для UvmRequestType
#include "uvm_types.h" // Для UvmResponseMessage, UvmSvmLink, MessageType, MAX_SVM_INSTANCES
#include <pthread.h> // Для pthread_self, если понадобится для отладки
#include <unistd.h>  // Для usleep
#include <time.h>    // Для clock_gettime, timersub
#include <stdio.h>     // Для NULL
#include <string.h>
#include "../utils/ts_uvm_resp_queue.h" // Для ThreadSafeUvmRespQueue и uvq_dequeue
#include "../protocol/message_utils.h"   // Для get_full_message_number, message_to_host_byte_order
#include <arpa/inet.h>                  // Для ntohs, ntohl
#include "../config/config.h"            // Для MAX_SVM_INSTANCES (если он не определен в uvm_types.h напрямую)

extern ThreadSafeUvmRespQueue *uvm_incoming_response_queue;
extern volatile bool uvm_keep_running;
extern UvmSvmLink svm_links[MAX_SVM_INSTANCES]; // Нужен для обновления last_activity_time
extern pthread_mutex_t uvm_links_mutex;      // и для GUI
extern void send_to_gui_socket(const char *message_to_gui); // Для отправки RECV/EVENT

bool wait_for_specific_response(
    int target_svm_id,
    MessageType expected_msg_type,
    UvmResponseMessage *response_message_out, // Переименовал для ясности
    int timeout_ms
) {
    if (!uvm_incoming_response_queue || !response_message_out) {
        fprintf(stderr, "wait_for_specific_response: Invalid arguments (queue or output buffer is NULL).\n");
        return false;
    }

    struct timespec start_time, current_time, deadline;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    deadline = start_time;
    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    printf("UVM (SVM %d): Ожидание ответа типа %d (таймаут %d мс)...\n", target_svm_id, expected_msg_type, timeout_ms);

    UvmResponseMessage current_response_data; // Локальный буфер для dequeue

    while (uvm_keep_running) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        if (current_time.tv_sec > deadline.tv_sec || (current_time.tv_sec == deadline.tv_sec && current_time.tv_nsec >= deadline.tv_nsec)) {
            fprintf(stderr, "UVM (SVM %d): Таймаут ожидания ответа типа %d.\n", target_svm_id, expected_msg_type);
            return false; // Таймаут
        }

        // Пытаемся извлечь сообщение из очереди (неблокирующе или с очень малым таймаутом для dequeue)
        // Для простоты пока делаем условное неблокирующее извлечение с помощью флага shutdown в очереди
        // и небольшой паузы, если очередь пуста.
        // Более корректно было бы использовать pthread_cond_timedwait для самой очереди,
        // но это усложнит ts_uvm_resp_queue.c
        if (uvq_dequeue(uvm_incoming_response_queue, &current_response_data)) {
            // Сообщение получено
            if (current_response_data.source_svm_id == target_svm_id) {
                // Обновляем время активности для этого SVM
                pthread_mutex_lock(&uvm_links_mutex);
                if (target_svm_id >= 0 && target_svm_id < MAX_SVM_INSTANCES) {
                    svm_links[target_svm_id].last_activity_time = time(NULL);
                    // Можно также обновить last_recv_msg_type/num/time и last_recv_bcb,
                    // но это лучше делать в основном цикле main после успешного wait_for_specific_response
                }
                pthread_mutex_unlock(&uvm_links_mutex);

                if (current_response_data.message.header.message_type == expected_msg_type) {
                    // Это ожидаемый ответ!
                    memcpy(response_message_out, &current_response_data, sizeof(UvmResponseMessage));
                    printf("UVM (SVM %d): Получен ожидаемый ответ типа %d.\n", target_svm_id, expected_msg_type);
                    return true;
                } else {
                    // Сообщение от нужного SVM, но не того типа.
                    // Это может быть асинхронное сообщение (например, "Предупреждение") или ошибка.
                    fprintf(stderr, "UVM (SVM %d): Получено сообщение типа %d (номер %u), ожидался тип %d. Обрабатываем как асинхронное...\n",
                           target_svm_id,
                           current_response_data.message.header.message_type,
                           get_full_message_number(&current_response_data.message.header),
                           expected_msg_type);

                    // Отправляем это "неожиданное" сообщение в GUI
                    char gui_buffer[512];
                    char bcb_field[32] = "";
                    char details_field[256] = "";
                    bool bcb_present = false;
                    // (Здесь нужна логика извлечения BCB и деталей из current_response_data.message, как в main)
                     // ---- Начало блока извлечения деталей для GUI ----
                    pthread_mutex_lock(&uvm_links_mutex); // Для доступа к svm_links[target_svm_id]
                    UvmSvmLink *link_for_gui_event = &svm_links[target_svm_id];

                    switch(current_response_data.message.header.message_type) {
                         case MESSAGE_TYPE_CONFIRM_INIT: // Не должно быть здесь, если ждем другой тип
                         case MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA:
                         case MESSAGE_TYPE_RESULTATY_KONTROLYA:
                         case MESSAGE_TYPE_SOSTOYANIE_LINII:
                         case MESSAGE_TYPE_PREDUPREZHDENIE:
                            // Эта логика извлечения BCB и деталей должна быть более общей
                            // и, возможно, вынесена в отдельную функцию.
                            // Пока что, если это Предупреждение, извлечем TKS
                            if (current_response_data.message.header.message_type == MESSAGE_TYPE_PREDUPREZHDENIE &&
                                ntohs(current_response_data.message.header.body_length) >= sizeof(PreduprezhdenieBody)) {
                                PreduprezhdenieBody *warn_body = (PreduprezhdenieBody*)current_response_data.message.body;
                                message_to_host_byte_order(&current_response_data.message); // Преобразуем для чтения
                                snprintf(details_field, sizeof(details_field), "TKS=%u", warn_body->tks);
                                snprintf(bcb_field, sizeof(bcb_field), ";BCB:0x%08X", ntohl(warn_body->bcb)); // BCB есть в Предупреждении
                                bcb_present = true;
                                link_for_gui_event->last_warning_tks = warn_body->tks; // Обновляем для GUI EVENT
                                link_for_gui_event->last_warning_time = time(NULL);
                                if(link_for_gui_event->status == UVM_LINK_ACTIVE) link_for_gui_event->status = UVM_LINK_WARNING;

                                // Отправка EVENT для Предупреждения
                                char gui_event_warn[128];
                                snprintf(gui_event_warn, sizeof(gui_event_warn), "EVENT;SVM_ID:%d;Type:Warning;Details:TKS=%u", target_svm_id, warn_body->tks);
                                send_to_gui_socket(gui_event_warn);
                                // И обновление статуса линка
                                snprintf(gui_event_warn, sizeof(gui_event_warn), "EVENT;SVM_ID:%d;Type:LinkStatus;Details:NewStatus=%d,AssignedLAK=0x%02X", target_svm_id, link_for_gui_event->status, link_for_gui_event->assigned_lak);
                                send_to_gui_socket(gui_event_warn);
                            } else {
                                // Для других типов пока без деталей, если это асинхронное
                                strcpy(details_field, "Async msg");
                            }
                            break;
                        default:
                            strcpy(details_field, "Async unknown type");
                            break;
                    }
                    pthread_mutex_unlock(&uvm_links_mutex);
                    // ---- Конец блока извлечения деталей для GUI ----

                    snprintf(gui_buffer, sizeof(gui_buffer),
                             "RECV;SVM_ID:%d;Type:%d;Num:%u;LAK:0x%02X%s;Details:%s",
                             target_svm_id,
                             current_response_data.message.header.message_type,
                             get_full_message_number(&current_response_data.message.header),
                             current_response_data.message.header.address, // Адрес отправителя (SVM)
                             bcb_present ? bcb_field : "",
                             details_field);
                    send_to_gui_socket(gui_buffer);
                    // Продолжаем ожидать нужный ответ
                }
            } else {
                // Сообщение от другого SVM. Пока просто логируем и игнорируем в контексте ожидания.
                // В идеале, его нужно было бы сохранить и обработать в основном цикле.
                printf("UVM (SVM %d): Во время ожидания ответа получено сообщение от SVM %d (тип %d). Игнорируется в этом контексте.\n",
                       target_svm_id, current_response_data.source_svm_id, current_response_data.message.header.message_type);
                 // Отправляем это "постороннее" сообщение в GUI, чтобы не потерять
                char gui_buffer_other[512];
                 // (Нужна похожая логика извлечения деталей и BCB, если они есть)
                snprintf(gui_buffer_other, sizeof(gui_buffer_other),
                         "RECV;SVM_ID:%d;Type:%d;Num:%u;LAK:0x%02X;Details:Forwarded during wait for SVM %d",
                         current_response_data.source_svm_id,
                         current_response_data.message.header.message_type,
                         get_full_message_number(&current_response_data.message.header),
                         current_response_data.message.header.address,
                         target_svm_id);
                send_to_gui_socket(gui_buffer_other);
            }
        } else {
            // Очередь пуста (и не закрыта)
            if (!uvm_keep_running) break; // Если пришел сигнал на завершение
            usleep(10000); // Небольшая пауза, чтобы не загружать CPU впустую
        }
    } // end while

    // Если вышли из цикла из-за !uvm_keep_running
    return false;
}

const char* uvm_request_type_to_message_name(UvmRequestType req_type) {
    switch (req_type) {
        case UVM_REQ_INIT_CHANNEL:      return "Инициализация канала"; // Соответствует MESSAGE_TYPE_INIT_CHANNEL
        case UVM_REQ_PROVESTI_KONTROL:    return "Провести контроль";    // Соответствует MESSAGE_TYPE_PROVESTI_KONTROL
        case UVM_REQ_VYDAT_REZULTATY:   return "Выдать результаты контроля"; // Соответствует MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA
        case UVM_REQ_VYDAT_SOSTOYANIE:  return "Выдать состояние линии"; // Соответствует MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII
        case UVM_REQ_PRIYAT_PARAM_SO:   return "Принять параметры СО";   // Соответствует MESSAGE_TYPE_PRIYAT_PARAMETRY_SO
        case UVM_REQ_PRIYAT_TIME_REF:   return "Принять TIME_REF_RANGE"; // Соответствует MESSAGE_TYPE_PRIYAT_TIME_REF_RANGE
        case UVM_REQ_PRIYAT_REPER:      return "Принять Reper";          // Соответствует MESSAGE_TYPE_PRIYAT_REPER
        case UVM_REQ_PRIYAT_PARAM_SDR:  return "Принять параметры СДР";  // Соответствует MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR
        case UVM_REQ_PRIYAT_PARAM_3TSO: return "Принять параметры 3ЦО";  // Соответствует MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO
        case UVM_REQ_PRIYAT_REF_AZIMUTH:return "Принять REF_AZIMUTH";    // Соответствует MESSAGE_TYPE_PRIYAT_REF_AZIMUTH
        case UVM_REQ_PRIYAT_PARAM_TSD:  return "Принять параметры ЦДР";  // Соответствует MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD
        case UVM_REQ_PRIYAT_NAV_DANNYE: return "Навигационные данные";   // Соответствует MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE
        case UVM_REQ_SHUTDOWN:          return "Запрос Shutdown";
        case UVM_REQ_NONE:
        default:                        return "Неизвестный запрос UVM";
    }
}