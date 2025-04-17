/*
 * svm/svm_handlers.h
 *
 * Описание:
 * Объявления функций-обработчиков для входящих сообщений SVM
 * и функции инициализации диспетчера сообщений.
 */

#ifndef SVM_HANDLERS_H
#define SVM_HANDLERS_H

#include "../protocol/protocol_defs.h" // Путь может отличаться

// --- Тип указателя на функцию-обработчик ---
typedef void (*MessageHandler)(int clientSocketFD, Message *message);

// --- Глобальный массив указателей (объявляем как extern) ---
extern MessageHandler message_handlers[256];

// --- Прототипы функций-обработчиков ---
// Их нужно сделать видимыми, чтобы svm_main.c мог их вызывать через массив
void handle_init_channel_message(int clientSocketFD, Message *receivedMessage);
void handle_confirm_init_message(int clientSocketFD, Message *receivedMessage);
void handle_provesti_kontrol_message(int clientSocketFD, Message *receivedMessage);
void handle_podtverzhdenie_kontrolya_message(int clientSocketFD, Message *receivedMessage);
void handle_vydat_rezultaty_kontrolya_message(int clientSocketFD, Message *receivedMessage);
void handle_rezultaty_kontrolya_message(int clientSocketFD, Message *receivedMessage);
void handle_vydat_sostoyanie_linii_message(int clientSocketFD, Message *receivedMessage);
void handle_sostoyanie_linii_message(int clientSocketFD, Message *receivedMessage);
void handle_prinyat_parametry_so_message(int clientSocketFD, Message *receivedMessage);
void handle_prinyat_time_ref_range_message(int clientSocketFD, Message *receivedMessage);
void handle_prinyat_reper_message(int clientSocketFD, Message *receivedMessage);
void handle_prinyat_parametry_sdr_message(int clientSocketFD, Message *receivedMessage);
void handle_prinyat_parametry_3tso_message(int clientSocketFD, Message *receivedMessage);
void handle_prinyat_ref_azimuth_message(int clientSocketFD, Message *receivedMessage);
void handle_prinyat_parametry_tsd_message(int clientSocketFD, Message *receivedMessage);
void handle_navigatsionnye_dannye_message(int clientSocketFD, Message *receivedMessage);
// ... Добавить прототипы для остальных обработчиков ...

// --- Функция инициализации диспетчера ---
/**
 * @brief Инициализирует массив указателей на функции-обработчики сообщений.
 */
void init_message_handlers(void);

#endif // SVM_HANDLERS_H