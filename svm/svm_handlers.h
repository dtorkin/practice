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
#include "../io/io_interface.h" // <-- Включаем для IOInterface

// --- Тип указателя на функцию-обработчик ---
typedef void (*MessageHandler)(IOInterface *io, int clientSocketFD, Message *message); // <-- Добавлен IOInterface *io

// --- Глобальный массив указателей (объявляем как extern) ---
extern MessageHandler message_handlers[256];

// --- Прототипы функций-обработчиков (добавлен IOInterface *io) ---
void handle_init_channel_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_confirm_init_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_provesti_kontrol_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_podtverzhdenie_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_vydat_rezultaty_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_rezultaty_kontrolya_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_vydat_sostoyanie_linii_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_sostoyanie_linii_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_prinyat_parametry_so_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_prinyat_time_ref_range_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_prinyat_reper_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_prinyat_parametry_sdr_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_prinyat_parametry_3tso_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_prinyat_ref_azimuth_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_prinyat_parametry_tsd_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
void handle_navigatsionnye_dannye_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
// Добавьте сюда прототипы для остальных сообщений от СВМ к УВМ, если они понадобятся
// void handle_subk_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);
// ...
// void handle_preduprezhdenie_message(IOInterface *io, int clientSocketFD, Message *receivedMessage);


// --- Функция инициализации диспетчера ---
void init_message_handlers(void);

#endif // SVM_HANDLERS_H