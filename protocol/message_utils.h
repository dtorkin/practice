/*
 * protocol/message_utils.h
 *
 * Описание:
 * Прототипы утилитных функций для работы со структурами сообщений:
 * - Получение полного номера сообщения.
 * - Преобразование порядка байт (network/host).
 */

#ifndef MESSAGE_UTILS_H
#define MESSAGE_UTILS_H

#include "protocol_defs.h"

// Получить полный номер сообщения (биты 8-10 из флагов + биты 0-7 из номера)
uint16_t get_full_message_number(const MessageHeader *header);

// Преобразовать поля сообщения (header.body_length и поля тела)
// из Host Byte Order в Network Byte Order перед отправкой.
void message_to_network_byte_order(Message *message);

// Преобразовать поля сообщения (header.body_length и поля тела)
// из Network Byte Order в Host Byte Order после получения.
void message_to_host_byte_order(Message *message);

#endif // MESSAGE_UTILS_H