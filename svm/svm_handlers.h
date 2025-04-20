/*
 * svm/svm_handlers.h
 *
 * Описание:
 * Объявления функций-обработчиков для входящих сообщений SVM
 * и функции инициализации диспетчера сообщений.
 * МОДИФИЦИРОВАНО для работы с экземплярами SVM.
 */

#ifndef SVM_HANDLERS_H
#define SVM_HANDLERS_H

#include "../protocol/protocol_defs.h"
#include "../io/io_interface.h" // Не используется напрямую, но может быть полезно для контекста
#include "svm_types.h" // <-- ВКЛЮЧЕНО для SvmInstance

// --- Тип указателя на функцию-обработчик ---
// Теперь принимает SvmInstance* и Message*, возвращает Message* (ответ) или NULL
typedef Message* (*MessageHandler)(SvmInstance *instance, Message *message);

// --- Глобальный массив указателей (объявляем как extern) ---
extern MessageHandler message_handlers[256];

// --- Прототипы функций-обработчиков (теперь принимают SvmInstance*) ---
Message* handle_init_channel_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_confirm_init_message(SvmInstance *instance, Message *receivedMessage); // Заглушка
Message* handle_provesti_kontrol_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_podtverzhdenie_kontrolya_message(SvmInstance *instance, Message *receivedMessage); // Заглушка
Message* handle_vydat_rezultaty_kontrolya_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_rezultaty_kontrolya_message(SvmInstance *instance, Message *receivedMessage); // Заглушка
Message* handle_vydat_sostoyanie_linii_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_sostoyanie_linii_message(SvmInstance *instance, Message *receivedMessage); // Заглушка
Message* handle_prinyat_parametry_so_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_time_ref_range_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_reper_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_parametry_sdr_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_parametry_3tso_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_ref_azimuth_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_prinyat_parametry_tsd_message(SvmInstance *instance, Message *receivedMessage);
Message* handle_navigatsionnye_dannye_message(SvmInstance *instance, Message *receivedMessage);
// ... Добавить прототипы для остальных обработчиков ...

// --- Функция инициализации диспетчера ---
void init_message_handlers(void);

#endif // SVM_HANDLERS_H