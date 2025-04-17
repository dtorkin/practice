/*
 * uvm/uvm_sender.c
 * ... (includes) ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <stdbool.h>
#include <arpa/inet.h> // Для ntohs

#include "uvm_types.h"
#include "../utils/ts_queue_req.h"
#include "../protocol/protocol_defs.h"
#include "../protocol/message_builder.h"
#include "../io/io_common.h"
#include "../io/io_interface.h"


// --- Внешние переменные (из uvm_main) ---
extern IOInterface *io_uvm;
extern ThreadSafeReqQueue *uvm_outgoing_request_queue;
extern volatile bool uvm_keep_running;
// --- Добавляем доступ к счетчику и cond var ---
extern pthread_mutex_t uvm_send_counter_mutex;
extern pthread_cond_t  uvm_send_counter_cond;
extern volatile int    uvm_outstanding_sends;

// --- Вспомогательная функция для вывода байт ---
static void print_body_preview(const Message *msg, UvmRequestType req_type) {
    const char* msg_name_str = uvm_request_type_to_message_name(req_type);
    printf("Данные тела сообщения '%s' (первые 20 байт): ", msg_name_str);

    // Используем -> для доступа к членам через указатель
    uint16_t len = ntohs(msg->header.body_length); // Преобразуем для сравнения
    for (int i = 0; i < 20 && i < len; ++i) {
		printf("%02X ", msg->body[i]); // Используем ->
	}
	if (len > 20) printf("...");
	printf("\n");
}


void* uvm_sender_thread_func(void* arg) {
    (void)arg;
    printf("UVM Sender thread started.\n");
    UvmRequest request;
    Message messageToSend;
    uint16_t msgCounter = 0;

    while (true) {
        if (!queue_req_dequeue(uvm_outgoing_request_queue, &request)) {
            if (!uvm_keep_running && uvm_outgoing_request_queue->count == 0) break;
            if(uvm_keep_running) { usleep(10000); continue; }
            else { break; }
        }

        if (request.type == UVM_REQ_SHUTDOWN) { // Проверяем команду shutdown
             printf("Sender Thread: Получен запрос на завершение.\n");
             break; // Выходим из цикла
        }

        bool need_send = true;
        switch (request.type) {
            case UVM_REQ_INIT_CHANNEL:
                messageToSend = create_init_channel_message(LOGICAL_ADDRESS_UVM_VAL, LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, msgCounter++);
                break;
            case UVM_REQ_PROVESTI_KONTROL:
                messageToSend = create_provesti_kontrol_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, request.tk_param, msgCounter++);
                break;
            case UVM_REQ_VYDAT_REZULTATY:
                 messageToSend = create_vydat_rezultaty_kontrolya_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, request.vpk_param, msgCounter++);
                 break;
            case UVM_REQ_VYDAT_SOSTOYANIE:
                 messageToSend = create_vydat_sostoyanie_linii_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, msgCounter++);
                 break;
            case UVM_REQ_PRIYAT_PARAM_SO:
                 messageToSend = create_prinyat_parametry_so_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, msgCounter++);
                 break;
            case UVM_REQ_PRIYAT_TIME_REF:
                 messageToSend = create_prinyat_time_ref_range_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, msgCounter++);
                 break;
            case UVM_REQ_PRIYAT_REPER:
                 messageToSend = create_prinyat_reper_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, msgCounter++);
                 break;
            case UVM_REQ_PRIYAT_PARAM_SDR:
                 messageToSend = create_prinyat_parametry_sdr_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, msgCounter++);
                 // TODO: Добавить HRR массив и обновить длину
                 break;
            case UVM_REQ_PRIYAT_PARAM_3TSO:
                 messageToSend = create_prinyat_parametry_3tso_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, msgCounter++);
                 break;
            case UVM_REQ_PRIYAT_REF_AZIMUTH:
                 messageToSend = create_prinyat_ref_azimuth_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, msgCounter++);
                 break;
            case UVM_REQ_PRIYAT_PARAM_TSD:
                  messageToSend = create_prinyat_parametry_tsd_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, msgCounter++);
                  // TODO: Добавить OKM, HShMR, HAR массивы и обновить длину
                  break;
            case UVM_REQ_PRIYAT_NAV_DANNYE:
                 messageToSend = create_navigatsionnye_dannye_message(LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL, msgCounter++);
                 break;
			default:
                fprintf(stderr, "Sender Thread: Неизвестный тип запроса: %d\n", request.type);
                need_send = false;
                // Уменьшаем счетчик, т.к. отправки не будет
                pthread_mutex_lock(&uvm_send_counter_mutex);
                if (uvm_outstanding_sends > 0) {
                     uvm_outstanding_sends--;
                }
                 // Сигналим Main на всякий случай, если он ждет нуля
                if (uvm_outstanding_sends == 0) {
                    pthread_cond_signal(&uvm_send_counter_cond);
                }
                pthread_mutex_unlock(&uvm_send_counter_mutex);
                break;
        }

        if (!uvm_keep_running) break;


        if (need_send) {
            // --- Логика отправки ---
            messageToSend.header.body_length = ntohs(messageToSend.header.body_length); // В хост для print_body_preview
            print_body_preview(&messageToSend, request.type);
            messageToSend.header.body_length = htons(messageToSend.header.body_length); // Обратно в сеть для send

            if (send_protocol_message(io_uvm, io_uvm->io_handle, &messageToSend) != 0) {
                fprintf(stderr, "Sender Thread: Ошибка отправки сообщения (тип запроса %d). Завершение.\n", request.type);
                if(uvm_keep_running) { uvm_keep_running = false; /* ... */ }
                // Не уменьшаем счетчик при ошибке, Main увидит ошибку и выйдет
                break;
            }
            printf("Отправлено сообщение '%s'\n", uvm_request_type_to_message_name(request.type));

            // --- Уменьшаем счетчик и сигналим Main после УСПЕШНОЙ отправки ---
            pthread_mutex_lock(&uvm_send_counter_mutex);
            if (uvm_outstanding_sends > 0) {
                uvm_outstanding_sends--;
                printf("Sender Thread: Сообщение отправлено, осталось %d\n", uvm_outstanding_sends);
            } else {
                // Этого не должно быть, если запросы добавляются корректно
                fprintf(stderr, "Sender Thread: Warning - outstanding sends counter is already zero!\n");
            }
            // Если счетчик достиг нуля, сигналим Main, что все отправлено
            if (uvm_outstanding_sends == 0) {
                 printf("Sender Thread: Все ожидающие сообщения отправлены, сигналим Main.\n");
                pthread_cond_signal(&uvm_send_counter_cond);
            }
            pthread_mutex_unlock(&uvm_send_counter_mutex);
            // --- Конец блока счетчика ---
        }
         // Выход из цикла по keep_running проверяется в начале следующей итерации
    }

    printf("UVM Sender thread finished.\n");
    return NULL;
}