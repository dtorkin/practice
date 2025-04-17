/*
 * svm/svm_handlers.h
 *
 * Описание:
 * Объявления функций-обработчиков для входящих сообщений SVM
 * и функции инициализации диспетчера сообщений.
 */

#ifndef SVM_HANDLERS_H
#define SVM_HANDLERS_H

#include "../protocol/protocol_defs.h"
#include "../io/io_interface.h"

// --- Тип указателя на функцию-обработчик ---
// Теперь возвращает указатель на Message (ответ) или NULL
typedef Message* (*MessageHandler)(IOInterface *io, int clientSocketFD, Message *message); // <-- Возвращает Message*

// --- Глобальный массив указателей (объявляем как extern) ---
extern MessageHandler message_handlers[256];

// --- Прототипы функций-обработчиков (теперь возвращают Message*) ---
Message* handle_init_channel_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_confirm_init_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_provesti_kontrol_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_podtverzhdenie_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_vydat_rezultaty_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_rezultaty_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_vydat_sostoyanie_linii_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_sostoyanie_linii_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_prinyat_parametry_so_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_prinyat_time_ref_range_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_prinyat_reper_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_prinyat_parametry_sdr_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_prinyat_parametry_3tso_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_prinyat_ref_azimuth_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_prinyat_parametry_tsd_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
Message* handle_navigatsionnye_dannye_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
// ... Добавить прототипы для остальных обработчиков, если они будут возвращать ответы ...

// --- Функция инициализации диспетчера ---
void init_message_handlers(void);

#endif // SVM_HANDLERS_H