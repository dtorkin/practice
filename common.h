/*
 * common.h
 *
 * Описание:
 * Содержит общие элементы, которые (пока) не вынесены в другие модули.
 * В будущем этот файл может быть удален или пересмотрен.
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include "protocol/protocol_defs.h" // Включаем определения протокола

// Прототип функции отправки сообщения (будет перемещен в io/io_common.h)
int send_message(int socketFD, Message *message);

#endif // COMMON_H