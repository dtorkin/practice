/*
 * svm/svm_handlers.c
 * Описание: Реализации функций-обработчиков для входящих сообщений SVM.
 * (Версия для одного процесса, работает с SvmInstance, читает флаги сбоев из instance)
 */
#include "svm_handlers.h"
#include "svm_timers.h" // Для get_instance_*
#include "../protocol/message_builder.h"
#include "../protocol/message_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h> // Для ntohs

// Массив указателей
MessageHandler message_handlers[256];

// --- Реализации обработчиков (принимают SvmInstance*) ---

Message* handle_init_channel_message(SvmInstance *instance, Message *receivedMessage) {
    if (!instance || !receivedMessage) return NULL;

    printf("Processor (Inst %d): Обработка 'Инициализация канала'\n", instance->id);
    InitChannelBody *req_body = (InitChannelBody *)receivedMessage->body;

    // Проверяем запрошенный LAK (хотя он уже установлен при инициализации instance)
    if (req_body->lak != instance->assigned_lak) {
         fprintf(stderr,"  Warning (Inst %d): Requested LAK 0x%02X differs from assigned LAK 0x%02X.\n",
                instance->id, req_body->lak, instance->assigned_lak);
         // Продолжаем с назначенным LAK
    } else {
         printf("  Параметры: LAUVM=0x%01X, LAK=0x%02X (assigned)\n", req_body->lauvm, instance->assigned_lak);
    }

    // --- Проверка на имитацию сбоя ---
    if (instance->send_warning_on_confirm) {
         printf("  SVM (Inst %d, LAK 0x%02X): SIMULATING warning instead of confirm init (TKS=%u).\n",
                instance->id, instance->assigned_lak, instance->warning_tks);
         Message *warnMsg = malloc(sizeof(Message));
         if (!warnMsg) { perror("handle_init_channel: malloc warnMsg"); return NULL; }
         uint8_t pks_dummy[6] = {0};
         uint32_t bcb = get_instance_bcb_counter(instance); // Получаем счетчик BCB
         *warnMsg = create_preduprezhdenie_message(instance->assigned_lak, instance->warning_tks, pks_dummy, bcb, instance->message_counter++);
         return warnMsg; // Возвращаем ПРЕДУПРЕЖДЕНИЕ
    }
    // --- Конец проверки на имитацию сбоя ---

    printf("  SVM (Inst %d): Эмуляция выключения лазера...\n", instance->id);
    uint8_t slp = 0x03, vdr = 0x10, bop1 = 0x11, bop2 = 0x12;
    uint32_t current_bcb = get_instance_bcb_counter(instance);

    Message *responseMessage = (Message*)malloc(sizeof(Message));
    if (!responseMessage) { perror("handle_init_channel: malloc response"); return NULL; }

    *responseMessage = create_confirm_init_message(
                                instance->assigned_lak,
                                slp, vdr, bop1, bop2, current_bcb,
                                instance->message_counter++); // Счетчик экземпляра

    printf("  Ответ 'Подтверждение инициализации' сформирован (LAK=0x%02X).\n", instance->assigned_lak);

    pthread_mutex_lock(&instance->instance_mutex);
    instance->current_state = STATE_INITIALIZED;
    pthread_mutex_unlock(&instance->instance_mutex);
    return responseMessage;
}

Message* handle_provesti_kontrol_message(SvmInstance *instance, Message *receivedMessage) {
    if (!instance || !receivedMessage) return NULL;
    printf("Processor (Inst %d): Обработка 'Провести контроль'\n", instance->id);

    pthread_mutex_lock(&instance->instance_mutex);
    instance->current_state = STATE_SELF_TEST;
    pthread_mutex_unlock(&instance->instance_mutex);
    printf("  SVM (Inst %d): Эмуляция самопроверки...\n", instance->id);
    sleep(1); // Обычная задержка на самопроверку
    pthread_mutex_lock(&instance->instance_mutex);
    instance->current_state = STATE_INITIALIZED;
    pthread_mutex_unlock(&instance->instance_mutex);

    // --- Проверка на имитацию таймаута (задержка или полное прекращение ответа) ---
    if (instance->simulate_response_timeout) {
        // Для SVM, который должен имитировать "зависание",
        // мы здесь устанавливаем флаг, чтобы он перестал отвечать НА СЛЕДУЮЩИЕ команды.
        // А на эту команду он ответит, но с задержкой.
        printf("  SVM (Inst %d, LAK 0x%02X): SIMULATING response delay for 'Podtverzhdenie Kontrolya' AND preparing to stop responding.\n",
               instance->id, instance->assigned_lak);
        pthread_mutex_lock(&instance->instance_mutex);
        instance->user_flag1 = true; // Устанавливаем флаг "перестать отвечать"
        pthread_mutex_unlock(&instance->instance_mutex);
        sleep(10); // Имитируем задержку ответа на ЭТУ команду
    }
    // --- Конец проверки ---

    ProvestiKontrolBody *req_body = (ProvestiKontrolBody *)receivedMessage->body;
    uint32_t current_bcb = get_instance_bcb_counter(instance);

    Message *responseMessage = (Message*)malloc(sizeof(Message));
    if (!responseMessage) {
        perror("handle_provesti_kontrol_message: malloc failed");
        return NULL;
    }
    *responseMessage = create_podtverzhdenie_kontrolya_message(
                                instance->assigned_lak, req_body->tk, current_bcb,
                                instance->message_counter++);
    printf("  Ответ 'Подтверждение контроля' сформирован.\n");
    return responseMessage;
}

Message* handle_vydat_rezultaty_kontrolya_message(SvmInstance *instance, Message *receivedMessage) {
    if (!instance || !receivedMessage) return NULL;

    // --- Проверка, не должен ли этот экземпляр перестать отвечать ---
    pthread_mutex_lock(&instance->instance_mutex);
    bool should_not_respond = (instance->simulate_response_timeout && instance->user_flag1);
    pthread_mutex_unlock(&instance->instance_mutex);

    if (should_not_respond) {
        printf("Processor (Inst %d, LAK 0x%02X): SIMULATING NO RESPONSE for 'Vydat Rezultaty Kontrolya' due to timeout simulation.\n",
               instance->id, instance->assigned_lak);
        return NULL; // Не отправляем ответ
    }
    // --- Конец проверки ---

    printf("Processor (Inst %d): Обработка 'Выдать результаты контроля'\n", instance->id);
    (void)receivedMessage;

    // --- Проверка на имитацию сбоя контроля ---
    uint8_t rsk = 0x3F; // По умолчанию ОК
    if (instance->simulate_control_failure) {
         rsk = 0x3E;
         printf("  SVM (Inst %d, LAK 0x%02X): SIMULATING control failure (RSK=0x%02X).\n", instance->id, instance->assigned_lak, rsk);
    }
    // --- Конец проверки ---

    // Задержка для simulate_response_timeout здесь не нужна, так как мы либо уже вышли (should_not_respond),
    // либо этот экземпляр не должен имитировать таймаут для этой команды.
    // Если нужно имитировать задержку на *каждый* ответ, то блок if(instance->simulate_response_timeout) { sleep(10); }
    // нужно ставить в начале каждой функции-обработчика с ответом.

    uint16_t vsk = 150;
    uint32_t current_bcb = get_instance_bcb_counter(instance);

    Message *responseMessage = (Message*)malloc(sizeof(Message));
    if (!responseMessage) {
        perror("handle_vydat_rezultaty_kontrolya_message: malloc failed");
        return NULL;
    }
    *responseMessage = create_rezultaty_kontrolya_message(
                                instance->assigned_lak, rsk, vsk, current_bcb,
                                instance->message_counter++);
    printf("  Ответ 'Результаты контроля' сформирован.\n");
    return responseMessage;
}

Message* handle_vydat_sostoyanie_linii_message(SvmInstance *instance, Message *receivedMessage) {
    if (!instance || !receivedMessage) return NULL;

    // --- Проверка, не должен ли этот экземпляр перестать отвечать ---
    pthread_mutex_lock(&instance->instance_mutex);
    bool should_not_respond = (instance->simulate_response_timeout && instance->user_flag1);
    pthread_mutex_unlock(&instance->instance_mutex);

    if (should_not_respond) {
        printf("Processor (Inst %d, LAK 0x%02X): SIMULATING NO RESPONSE for 'Vydat Sostoyanie Linii' due to timeout simulation.\n",
               instance->id, instance->assigned_lak);
        return NULL; // Не отправляем ответ
    }
    // --- Конец проверки ---

    printf("Processor (Inst %d): Обработка 'Выдать состояние линии'\n", instance->id);
    (void)receivedMessage;

    // Задержка для simulate_response_timeout здесь не нужна по тем же причинам.

    uint16_t kla_val;
    uint32_t sla_val_us100;
    uint16_t ksa_val;
    uint32_t current_bcb = get_instance_bcb_counter(instance);
    get_instance_line_status_counters(instance, &kla_val, &sla_val_us100, &ksa_val);

    Message *responseMessage = (Message*)malloc(sizeof(Message));
    if (!responseMessage) {
        perror("handle_vydat_sostoyanie_linii_message: malloc failed");
        return NULL;
    }
    *responseMessage = create_sostoyanie_linii_message(
                                instance->assigned_lak, kla_val, sla_val_us100, ksa_val, current_bcb,
                                instance->message_counter++);
    printf("  Ответ 'Состояние линии' сформирован.\n");
    return responseMessage;
}


// --- Заглушки и обработчики без ответа ---
// (Используют instance->id для логов)
Message* handle_confirm_init_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Подтверждение инициализации' (не ожидается) - ответа нет.\n", i?i->id:-1); return NULL; }
Message* handle_podtverzhdenie_kontrolya_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Подтверждение контроля' (не ожидается) - ответа нет.\n", i?i->id:-1); return NULL; }
Message* handle_rezultaty_kontrolya_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Результаты контроля' (не ожидается) - ответа нет.\n", i?i->id:-1); return NULL; }
Message* handle_sostoyanie_linii_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Состояние линии' (не ожидается) - ответа нет.\n", i?i->id:-1); return NULL; }
Message* handle_prinyat_parametry_so_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Принять параметры СО' (нет ответа).\n", i?i->id:-1); return NULL; }
Message* handle_prinyat_time_ref_range_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Принять TIME_REF_RANGE' (нет ответа).\n", i?i->id:-1); return NULL; }
Message* handle_prinyat_reper_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Принять Reper' (нет ответа).\n", i?i->id:-1); return NULL; }
Message* handle_prinyat_parametry_sdr_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Принять параметры СДР' (нет ответа).\n", i?i->id:-1); return NULL; }
Message* handle_prinyat_parametry_3tso_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Принять параметры 3ЦО' (нет ответа).\n", i?i->id:-1); return NULL; }
Message* handle_prinyat_ref_azimuth_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Принять REF_AZIMUTH' (нет ответа).\n", i?i->id:-1); return NULL; }
Message* handle_prinyat_parametry_tsd_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Принять параметры ЦДР' (нет ответа).\n", i?i->id:-1); return NULL; }
Message* handle_navigatsionnye_dannye_message(SvmInstance *i, Message *m) { (void)i; (void)m; printf("Processor (Inst %d): Обработка 'Навигационные данные' (нет ответа).\n", i?i->id:-1); return NULL; }

// --- Инициализация диспетчера ---
void init_message_handlers(void) {
	for (int i = 0; i < 256; ++i) {
		message_handlers[i] = NULL; // Обнуляем все указатели
	}
	// УВМ -> СВМ (Реальные обработчики)
	message_handlers[MESSAGE_TYPE_INIT_CHANNEL] = handle_init_channel_message;
	message_handlers[MESSAGE_TYPE_PROVESTI_KONTROL] = handle_provesti_kontrol_message;
	message_handlers[MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA] = handle_vydat_rezultaty_kontrolya_message;
	message_handlers[MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII] = handle_vydat_sostoyanie_linii_message;
	message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_SO] = handle_prinyat_parametry_so_message;
	message_handlers[MESSAGE_TYPE_PRIYAT_TIME_REF_RANGE] = handle_prinyat_time_ref_range_message;
	message_handlers[MESSAGE_TYPE_PRIYAT_REPER] = handle_prinyat_reper_message;
	message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR] = handle_prinyat_parametry_sdr_message;
	message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO] = handle_prinyat_parametry_3tso_message;
	message_handlers[MESSAGE_TYPE_PRIYAT_REF_AZIMUTH] = handle_prinyat_ref_azimuth_message;
	message_handlers[MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD] = handle_prinyat_parametry_tsd_message;
	message_handlers[MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE] = handle_navigatsionnye_dannye_message;

    // СВМ -> УВМ (Заглушки, так как SVM обычно не получает эти сообщения)
	message_handlers[MESSAGE_TYPE_CONFIRM_INIT] = handle_confirm_init_message;
	message_handlers[MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA] = handle_podtverzhdenie_kontrolya_message;
	message_handlers[MESSAGE_TYPE_RESULTATY_KONTROLYA] = handle_rezultaty_kontrolya_message;
	message_handlers[MESSAGE_TYPE_SOSTOYANIE_LINII] = handle_sostoyanie_linii_message;
    // ... Добавить остальные заглушки для сообщений СВМ->УВМ ...
    // message_handlers[MESSAGE_TYPE_SUBK] = handle_subk_message; // Пример
    // ...

    printf("Message handlers initialized.\n");
}