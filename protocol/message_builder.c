/*
 * protocol/message_builder.c
 *
 * Описание:
 * Реализации функций для создания различных типов сообщений протокола.
 * (Исправлено: Билдеры создают только заголовок с правильной длиной тела,
 *  само тело НЕ заполняется здесь).
 */

#include "message_builder.h"
#include <string.h>     // Для memset
#include <arpa/inet.h>  // Для htons, htonl

// Макрос для установки заголовка (устанавливает body_length = sizeof(body_struct_type))
#define SET_HEADER(msg, target_addr, direction, msg_num, msg_type, body_struct_type) \
    do { \
        memset(&(msg), 0, sizeof(Message)); \
        (msg).header.address = (target_addr); \
        (msg).header.flags.np = (direction); \
        (msg).header.flags.hc_t_bp = (((msg_num) >> 8) & 0x01); \
        (msg).header.flags.hc_ct_bp = (((msg_num) >> 9) & 0x01); \
        (msg).header.flags.hc_ct10p = (((msg_num) >> 10) & 0x01); \
        (msg).header.body_length = htons(sizeof(body_struct_type)); /* Устанавливаем длину */ \
        (msg).header.message_number = ((msg_num) & 0xFF); \
        (msg).header.message_type = (msg_type); \
    } while (0)

// Макрос для установки заголовка БЕЗ тела (body_length = 0)
#define SET_HEADER_NO_BODY(msg, target_addr, direction, msg_num, msg_type) \
    do { \
        memset(&(msg), 0, sizeof(Message)); \
        (msg).header.address = (target_addr); \
        (msg).header.flags.np = (direction); \
        (msg).header.flags.hc_t_bp = (((msg_num) >> 8) & 0x01); \
        (msg).header.flags.hc_ct_bp = (((msg_num) >> 9) & 0x01); \
        (msg).header.flags.hc_ct10p = (((msg_num) >> 10) & 0x01); \
        (msg).header.body_length = htons(0); /* Длина тела 0 */ \
        (msg).header.message_number = ((msg_num) & 0xFF); \
        (msg).header.message_type = (msg_type); \
    } while (0)

// --- Реализации функций создания сообщений (От УВМ к СВ-М) ---

// [4.2.1] «Инициализация канала»
Message create_init_channel_message(LogicalAddress uvm_address, LogicalAddress svm_address, uint16_t message_num) {
	Message message;
	// Устанавливаем заголовок. Тело будет заполнено в вызывающем коде.
	SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_INIT_CHANNEL, InitChannelBody);
    // Логика заполнения тела перенесена в uvm_main.c
    // InitChannelBody *body = (InitChannelBody *) message.body;
	// body->lauvm = uvm_address; // Делается в uvm_main.c
	// body->lak = svm_address;   // Делается в uvm_main.c
	return message;
}

// [4.2.3] «Провести контроль»
Message create_provesti_kontrol_message(LogicalAddress svm_address, uint8_t tk __attribute__((unused)), uint16_t message_num) {
    // Параметр tk больше не используется здесь, но оставлен для совместимости сигнатуры с uvm_main до рефакторинга
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PROVESTI_KONTROL, ProvestiKontrolBody);
    // Логика заполнения тела перенесена в uvm_main.c
	// ProvestiKontrolBody *body = (ProvestiKontrolBody *) message.body;
	// body->tk = tk; // Делается в uvm_main.c
	return message;
}

// [4.2.5] «Выдать результаты контроля»
Message create_vydat_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t vpk __attribute__((unused)), uint16_t message_num) {
    // Параметр vpk больше не используется здесь
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA, VydatRezultatyKontrolyaBody);
    // Логика заполнения тела перенесена в uvm_main.c
    // VydatRezultatyKontrolyaBody *body = (VydatRezultatyKontrolyaBody *) message.body;
    // body->vrk = vpk; // Делается в uvm_main.c
    return message;
}

// [4.2.7] «Выдать состояние линии»
Message create_vydat_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER_NO_BODY(message, svm_address, 0, message_num, MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII);
    return message;
}

// [4.2.9] «Принять параметры СО»
Message create_prinyat_parametry_so_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_PARAMETRY_SO, PrinyatParametrySoBody);
    // Тело НЕ заполняется здесь
    return message;
}

// [4.2.10] «Принять TIME_REF_RANGE»
Message create_prinyat_time_ref_range_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_TIME_REF_RANGE, PrinyatTimeRefRangeBody);
    // Тело НЕ заполняется здесь
    return message;
}

// [4.2.11] «Принять Reper»
Message create_prinyat_reper_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_REPER, PrinyatReperBody);
    // Тело НЕ заполняется здесь
    return message;
}

// [4.2.12] «Принять параметры СДР»
// ВНИМАНИЕ: Устанавливает длину тела РАВНОЙ БАЗОВОЙ СТРУКТУРЕ!
// Вызывающий код должен сам установить ПРАВИЛЬНУЮ длину тела после добавления массива HRR.
Message create_prinyat_parametry_sdr_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR, PrinyatParametrySdrBodyBase);
    // Тело НЕ заполняется здесь. Длина установлена по PrinyatParametrySdrBodyBase.
    // Вызывающий код ДОЛЖЕН обновить body_length после добавления HRR.
    return message;
}

// [4.2.13] «Принять параметры 3ЦО»
Message create_prinyat_parametry_3tso_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO, PrinyatParametry3TsoBody);
    // Тело НЕ заполняется здесь
    return message;
}

// [4.2.14] «Принять REF_AZIMUTH»
Message create_prinyat_ref_azimuth_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_REF_AZIMUTH, PrinyatRefAzimuthBody);
    // Тело НЕ заполняется здесь
    return message;
}

// [4.2.15] «Принять параметры ЦДР»
// ВНИМАНИЕ: Устанавливает длину тела РАВНОЙ БАЗОВОЙ СТРУКТУРЕ!
// Вызывающий код должен сам установить ПРАВИЛЬНУЮ длину тела после добавления массивов.
Message create_prinyat_parametry_tsd_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD, PrinyatParametryTsdBodyBase);
    // Тело НЕ заполняется здесь. Длина установлена по PrinyatParametryTsdBodyBase.
    // Вызывающий код ДОЛЖЕН обновить body_length после добавления массивов.
    return message;
}

// [4.2.16] «Навигационные данные»
Message create_navigatsionnye_dannye_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE, NavigatsionnyeDannyeBody);
    // Тело НЕ заполняется здесь
    return message;
}


// --- Реализации функций создания сообщений (От СВ-М к УВМ) ---
// Эти функции ДОЛЖНЫ принимать данные тела, так как их вызывает SVM,
// который ЗНАЕТ актуальные данные для ответа.

// [4.2.2] «Подтверждение инициализации канала»
Message create_confirm_init_message(LogicalAddress svm_address, uint8_t slp, uint8_t vdr, uint8_t bop1, uint8_t bop2, uint32_t bcb, uint16_t message_num) {
    Message message;
    SET_HEADER(message, LOGICAL_ADDRESS_UVM_VAL, 1, message_num, MESSAGE_TYPE_CONFIRM_INIT, ConfirmInitBody);
	ConfirmInitBody *body = (ConfirmInitBody *) message.body;
	body->lak = svm_address; // Подтверждаем адрес СВМ
	body->slp = slp;
	body->vdr = vdr;
	body->bop1 = bop1;
	body->bop2 = bop2;
	body->bcb = htonl(bcb); // Преобразуем BCB в сетевой порядок
	return message;
}

// [4.2.4] «Подтверждение контроля»
Message create_podtverzhdenie_kontrolya_message(LogicalAddress svm_address, uint8_t tk, uint32_t bcb, uint16_t message_num) {
    Message message;
    SET_HEADER(message, LOGICAL_ADDRESS_UVM_VAL, 1, message_num, MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA, PodtverzhdenieKontrolyaBody);
    PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody *) message.body;
    body->lak = svm_address;
    body->tk = tk;
    body->bcb = htonl(bcb);
    return message;
}

// [4.2.6] «Результаты контроля»
Message create_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t rsk, uint16_t vsk, uint32_t bcb, uint16_t message_num) {
    Message message;
    SET_HEADER(message, LOGICAL_ADDRESS_UVM_VAL, 1, message_num, MESSAGE_TYPE_RESULTATY_KONTROLYA, RezultatyKontrolyaBody);
    RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody *) message.body;
    body->lak = svm_address;
    body->rsk = rsk;
    body->vsk = htons(vsk);
    body->bcb = htonl(bcb);
    return message;
}

// [4.2.8] «Состояние линии»
Message create_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t kla, uint32_t sla, uint16_t ksa, uint32_t bcb, uint16_t message_num) {
    Message message;
    SET_HEADER(message, LOGICAL_ADDRESS_UVM_VAL, 1, message_num, MESSAGE_TYPE_SOSTOYANIE_LINII, SostoyanieLiniiBody);
	SostoyanieLiniiBody *body = (SostoyanieLiniiBody *) message.body;
	body->lak = svm_address;
	body->kla = htons(kla);
	body->sla = htonl(sla);
	body->ksa = htons(ksa);
	body->bcb = htonl(bcb);
	return message;
}

// [5.2] Сообщение "Предупреждение"
Message create_preduprezhdenie_message(LogicalAddress svm_address, uint8_t tks, const uint8_t* pks, uint32_t bcb, uint16_t message_num) {
    Message message;
    SET_HEADER(message, LOGICAL_ADDRESS_UVM_VAL, 1, message_num, MESSAGE_TYPE_PREDUPREZHDENIE, PreduprezhdenieBody);
    PreduprezhdenieBody *body = (PreduprezhdenieBody *) message.body;
    body->lak = svm_address;
    body->tks = tks;
    if (pks) { // Проверка на NULL
       memcpy(body->pks, pks, sizeof(body->pks));
    } else {
       memset(body->pks, 0, sizeof(body->pks)); // Обнуляем, если не передано
    }
    body->bcb = htonl(bcb);
    return message;
}

// ... Другие билдеры для сообщений SVM -> UVM должны быть реализованы аналогично,
//     принимая необходимые данные тела в качестве аргументов ...