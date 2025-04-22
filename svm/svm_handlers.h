/*
 * svm/svm_handlers.h
 *
 * Описание:
 * Объявления функций-обработчиков для входящих сообщений SVM
 * и функции инициализации диспетчера сообщений.
 * (Версия для SvmInstance, добавлена обертка extern "C")
 */

#ifndef SVM_HANDLERS_H
#define SVM_HANDLERS_H

#include "../protocol/protocol_defs.h"
#include "../io/io_interface.h"
// Включаем svm_types.h ДО extern "C", т.к. SvmInstance и Message используются в прототипах
#include "svm_types.h"

// --- Обертка для C++ ---
#ifdef __cplusplus
extern "C" {
#endif

// --- Объявления внутри extern "C" ---

// Тип указателя на функцию-обработчик
typedef Message* (*MessageHandler)(SvmInstance *instance, Message *message); // Используем SvmInstance напрямую

// Глобальный массив указателей
extern MessageHandler message_handlers[256];

// Прототипы функций-обработчиков
Message* handle_init_channel_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_confirm_init_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_provesti_kontrol_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_podtverzhdenie_kontrolya_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_vydat_rezultaty_kontrolya_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_rezultaty_kontrolya_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_vydat_sostoyanie_linii_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_sostoyanie_linii_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_parametry_so_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_time_ref_range_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_reper_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_parametry_sdr_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_parametry_3tso_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_ref_azimuth_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_parametry_tsd_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_navigatsionnye_dannye_message(SvmInstance *instance, Message *receivedMessage);
// ... Добавить прототипы для остальных обработчиков ...

// Функция инициализации диспетчера
void init_message_handlers(void);
// --- Конец объявлений ---

#ifdef __cplusplus
} // extern "C"
#endif
// --- Конец обертки ---

#endif // SVM_HANDLERS_H