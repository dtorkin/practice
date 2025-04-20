/*
 * svm/svm_handlers.c
 *
 * Описание:
 * Реализации функций-обработчиков для входящих сообщений SVM
 * и инициализация диспетчера сообщений.
 * МОДИФИЦИРОВАНО для работы с экземплярами SVM.
 */

#include "svm_handlers.h"
#include "svm_timers.h" // Для get_instance_* функций
#include "../protocol/message_builder.h"
#include "../protocol/message_utils.h"
// #include "../io/io_common.h" // Больше не нужен для send_protocol_message
#include <stdio.h>
#include <stdlib.h> // Для malloc, free
#include <string.h>
#include <unistd.h> // Для sleep
#include <arpa/inet.h> // Для ntohs/htons (хотя преобразование в очередях убрали)

// --- Глобальные переменные УДАЛЕНЫ (currentSvmState, currentMessageCounter) ---
// Они теперь часть SvmInstance

// --- Определение массива указателей ---
MessageHandler message_handlers[256];

// --- Реализации обработчиков (теперь принимают SvmInstance*) ---

// [4.2.1] «Инициализация канала» -> Отправляет [4.2.2] «Подтверждение инициализации»
Message* handle_init_channel_message(SvmInstance *instance, Message *receivedMessage) {
    if (!instance || !receivedMessage) return NULL;

	printf("Processor (Inst %d): Обработка 'Инициализация канала'\n", instance->id);
    uint16_t receivedMessageNumber = get_full_message_number(&receivedMessage->header);
    printf("  Номер полученного сообщения: %u\n", receivedMessageNumber);

	InitChannelBody *req_body = (InitChannelBody *)receivedMessage->body;

    // Проверяем, совпадает ли запрашиваемый LAK с назначенным этому экземпляру
    // (В реальной системе LAK может быть предложен УВМ, здесь мы его назначаем при подключении)
    if (req_body->lak != instance->assigned_lak) {
        fprintf(stderr,"  Warning (Inst %d): Requested LAK 0x%02X does not match assigned LAK 0x%02X. Using assigned LAK for response.\n",
                instance->id, req_body->lak, instance->assigned_lak);
        // Тем не менее, продолжим и ответим с назначенным LAK
    } else {
        printf("  Параметры: LAUVM=0x%02X, LAK=0x%02X (assigned)\n", req_body->lauvm, instance->assigned_lak);
    }

	printf("  SVM (Inst %d): Эмуляция выключения лазера...\n", instance->id);

    // Данные для ответа (можно сделать их частью SvmInstance, если они могут меняться)
	uint8_t slp = 0x03;
	uint8_t vdr = 0x10;
	uint8_t bop1 = 0x11;
	uint8_t bop2 = 0x12;
	uint32_t current_bcb = get_instance_bcb_counter(instance); // Используем функцию для экземпляра

    // Создаем ответное сообщение в динамической памяти
    Message *responseMessage = (Message*)malloc(sizeof(Message));
    if (!responseMessage) {
        perror("handle_init_channel: Failed to allocate memory for response");
        return NULL;
    }

    // Используем счетчик сообщений ЭТОГО экземпляра
	*responseMessage = create_confirm_init_message(
                                instance->assigned_lak, // Всегда отвечаем с назначенным LAK
                                slp, vdr, bop1, bop2,
                                current_bcb,
                                instance->message_counter++ // Инкрементируем счетчик экземпляра
                                );

	printf("  Ответ 'Подтверждение инициализации' сформирован.\n");

    // Обновляем состояние ЭТОГО экземпляра (под мьютексом)
    pthread_mutex_lock(&instance->instance_mutex);
	instance->current_state = STATE_INITIALIZED;
    pthread_mutex_unlock(&instance->instance_mutex);

    return responseMessage; // Возвращаем указатель на ответ
}

// [4.2.2] «Подтверждение инициализации канала» (заглушка, нет ответа)
Message* handle_confirm_init_message(SvmInstance *instance, Message *receivedMessage) {
	(void)instance;
    (void)receivedMessage;
    printf("Processor (Inst %d): Обработка 'Подтверждение инициализации' (не ожидается) - ответа нет.\n", instance ? instance->id : -1);
    return NULL; // Нет ответа
}

// [4.2.3] «Провести контроль» -> Отправляет [4.2.4] «Подтверждение контроля»
Message* handle_provesti_kontrol_message(SvmInstance *instance, Message *receivedMessage) {
    if (!instance || !receivedMessage) return NULL;
    printf("Processor (Inst %d): Обработка 'Провести контроль'\n", instance->id);

    ProvestiKontrolBody *req_body = (ProvestiKontrolBody *)receivedMessage->body;

    // Эмулируем контроль для этого экземпляра
    pthread_mutex_lock(&instance->instance_mutex);
    instance->current_state = STATE_SELF_TEST;
    pthread_mutex_unlock(&instance->instance_mutex);
	printf("  SVM (Inst %d): Эмуляция самопроверки...\n", instance->id);
	sleep(1); // Имитация задержки
    pthread_mutex_lock(&instance->instance_mutex);
	instance->current_state = STATE_INITIALIZED;
    pthread_mutex_unlock(&instance->instance_mutex);

    uint32_t current_bcb = get_instance_bcb_counter(instance);

    // Создаем ответ
    Message *responseMessage = (Message*)malloc(sizeof(Message));
     if (!responseMessage) {
        perror("handle_provesti_kontrol: Failed to allocate memory for response");
        return NULL;
    }
    *responseMessage = create_podtverzhdenie_kontrolya_message(
                                instance->assigned_lak, // Адрес этого SVM
                                req_body->tk, // Возвращаем тип контроля из запроса
                                current_bcb,
                                instance->message_counter++ // Инкремент счетчика экземпляра
                                );

	printf("  Ответ 'Подтверждение контроля' сформирован.\n");
    return responseMessage;
}

// [4.2.4] «Подтверждение контроля» (заглушка, нет ответа)
Message* handle_podtverzhdenie_kontrolya_message(SvmInstance *instance, Message *receivedMessage) {
    (void)instance;
    (void)receivedMessage;
    printf("Processor (Inst %d): Обработка 'Подтверждение контроля' (не ожидается) - ответа нет.\n", instance ? instance->id : -1);
    return NULL;
}

// [4.2.5] «Выдать результаты контроля» -> Отправляет [4.2.6] «Результаты контроля»
Message* handle_vydat_rezultaty_kontrolya_message(SvmInstance *instance, Message *receivedMessage) {
    if (!instance || !receivedMessage) return NULL;
    printf("Processor (Inst %d): Обработка 'Выдать результаты контроля'\n", instance->id);
    (void)receivedMessage; // Тело запроса пустое

	uint8_t rsk = 0x3F; // Все ОК (можно сделать зависимым от instance->id для теста)
	if (instance->id == 1) { // Если это второй экземпляр (ID=1)
    rsk = 0x3E; // Устанавливаем бит ошибки (например, младший бит = 0)
    printf("  SVM (Inst %d): Emulating control failure (RSK=0x%02X).\n", instance->id, rsk);
	uint16_t vsk = 150; // 150 мс
    uint32_t current_bcb = get_instance_bcb_counter(instance);

    // Создаем ответ
     Message *responseMessage = (Message*)malloc(sizeof(Message));
     if (!responseMessage) {
        perror("handle_vydat_rezultaty: Failed to allocate memory for response");
        return NULL;
    }
	*responseMessage = create_rezultaty_kontrolya_message(
                                instance->assigned_lak,
                                rsk,
                                vsk,
                                current_bcb,
                                instance->message_counter++
                                );

	printf("  Ответ 'Результаты контроля' сформирован.\n");
    return responseMessage;
}

// [4.2.6] «Результаты контроля» (заглушка, нет ответа)
Message* handle_rezultaty_kontrolya_message(SvmInstance *instance, Message *receivedMessage) {
	(void)instance;
    (void)receivedMessage;
    printf("Processor (Inst %d): Обработка 'Результаты контроля' (не ожидается) - ответа нет.\n", instance ? instance->id : -1);
    return NULL;
}

// [4.2.7] «Выдать состояние линии» -> Отправляет [4.2.8] «Состояние линии»
Message* handle_vydat_sostoyanie_linii_message(SvmInstance *instance, Message *receivedMessage) {
    if (!instance || !receivedMessage) return NULL;
    (void)receivedMessage; // Тело запроса пустое
	printf("Processor (Inst %d): Обработка 'Выдать состояние линии'\n", instance->id);

    uint16_t kla_val;
    uint32_t sla_val_us100;
    uint16_t ksa_val;
    uint32_t current_bcb = get_instance_bcb_counter(instance);
    // Получаем счетчики для ЭТОГО экземпляра
    get_instance_line_status_counters(instance, &kla_val, &sla_val_us100, &ksa_val);

    // Создаем ответ
    Message *responseMessage = (Message*)malloc(sizeof(Message));
     if (!responseMessage) {
        perror("handle_vydat_sostoyanie: Failed to allocate memory for response");
        return NULL;
    }
	*responseMessage = create_sostoyanie_linii_message(
                                instance->assigned_lak,
                                kla_val,
                                sla_val_us100, // Передаем значение в нужных единицах
                                ksa_val,
                                current_bcb,
                                instance->message_counter++
                                );
	printf("  Ответ 'Состояние линии' сформирован.\n");
    return responseMessage;
}

// [4.2.8] «Состояние линии» (заглушка, нет ответа)
Message* handle_sostoyanie_linii_message(SvmInstance *instance, Message *receivedMessage) {
    (void)instance;
    (void)receivedMessage;
	printf("Processor (Inst %d): Обработка 'Состояние линии' (не ожидается) - ответа нет.\n", instance ? instance->id : -1);
    return NULL;
}

// --- Обработчики сообщений без ответа (Принять ***) ---
// Просто логируем получение и игнорируем тело (пока)

Message* handle_prinyat_parametry_so_message(SvmInstance *instance, Message *receivedMessage) {
	(void)receivedMessage; // Игнорируем тело пока
    printf("Processor (Inst %d): Обработка 'Принять параметры СО' (нет ответа).\n", instance ? instance->id : -1);
    return NULL;
}

Message* handle_prinyat_time_ref_range_message(SvmInstance *instance, Message *receivedMessage) {
    (void)receivedMessage;
    printf("Processor (Inst %d): Обработка 'Принять TIME_REF_RANGE' (нет ответа).\n", instance ? instance->id : -1);
     return NULL;
}

Message* handle_prinyat_reper_message(SvmInstance *instance, Message *receivedMessage) {
    (void)receivedMessage;
    printf("Processor (Inst %d): Обработка 'Принять Reper' (нет ответа).\n", instance ? instance->id : -1);
     return NULL;
}

Message* handle_prinyat_parametry_sdr_message(SvmInstance *instance, Message *receivedMessage) {
    (void)receivedMessage;
    printf("Processor (Inst %d): Обработка 'Принять параметры СДР' (нет ответа).\n", instance ? instance->id : -1);
     return NULL;
}

Message* handle_prinyat_parametry_3tso_message(SvmInstance *instance, Message *receivedMessage) {
    (void)receivedMessage;
	printf("Processor (Inst %d): Обработка 'Принять параметры 3ЦО' (нет ответа).\n", instance ? instance->id : -1);
     return NULL;
}

Message* handle_prinyat_ref_azimuth_message(SvmInstance *instance, Message *receivedMessage) {
    (void)receivedMessage;
	printf("Processor (Inst %d): Обработка 'Принять REF_AZIMUTH' (нет ответа).\n", instance ? instance->id : -1);
    // Можно добавить проверку размера тела, если нужно
    return NULL;
}

Message* handle_prinyat_parametry_tsd_message(SvmInstance *instance, Message *receivedMessage) {
    (void)receivedMessage;
	printf("Processor (Inst %d): Обработка 'Принять параметры ЦДР' (нет ответа).\n", instance ? instance->id : -1);
     return NULL;
}

Message* handle_navigatsionnye_dannye_message(SvmInstance *instance, Message *receivedMessage) {
    (void)receivedMessage;
	printf("Processor (Inst %d): Обработка 'Навигационные данные' (нет ответа).\n", instance ? instance->id : -1);
     return NULL;
}

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