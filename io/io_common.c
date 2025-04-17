/*
 * io/io_common.c
 *
 * Описание:
 * Реализация общих функций для отправки и приема протокольных сообщений
 * через абстрактный интерфейс IOInterface.
 */

#include "io_common.h"
#include "../protocol/message_utils.h" // Для преобразования порядка байт
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
// #include <sys/socket.h> // Больше не нужны прямые вызовы сокетов здесь
// #include <arpa/inet.h>

// Отправить протокольное сообщение через интерфейс
int send_protocol_message(IOInterface *io, int handle, Message *message) {
    if (!io || !io->send_data || handle < 0 || !message) {
        fprintf(stderr, "send_protocol_message: Invalid arguments\n");
        return -1;
    }

	// Преобразуем сообщение в сетевой порядок байт
	message_to_network_byte_order(message);

	// Получаем актуальную длину тела (уже в сетевом порядке из-за вызова выше)
	uint16_t body_length_net = message->header.body_length;
    uint16_t body_length_host = ntohs(body_length_net);

    // Вычисляем общий размер сообщения для отправки
    size_t total_message_size = sizeof(MessageHeader) + body_length_host;

    printf("Отправка сообщения через %s: Тип=%u, Номер=%u, Длина тела=%u, Общий размер=%zu, Handle=%d\n",
           (io->type == IO_TYPE_ETHERNET) ? "Ethernet" : ((io->type == IO_TYPE_SERIAL) ? "Serial" : "Unknown"),
           message->header.message_type,
           get_full_message_number(&message->header),
           body_length_host,
           total_message_size,
           handle);

    // Отправляем данные через функцию интерфейса
    ssize_t bytes_sent = io->send_data(handle, message, total_message_size);

    // Преобразуем обратно в хостовый порядок в любом случае
    // (даже если была ошибка, чтобы структура была консистентной)
    message->header.body_length = body_length_net; // Восстанавливаем для правильного преобразования обратно
    message_to_host_byte_order(message);

    if (bytes_sent < 0) {
        // Ошибка уже должна быть выведена внутри io->send_data
        fprintf(stderr, "send_protocol_message: io->send_data failed\n");
        return -1;
    } else if ((size_t)bytes_sent != total_message_size) {
		fprintf(stderr, "send_protocol_message: Ошибка отправки: отправлено %zd байт вместо %zu\n", bytes_sent, total_message_size);
		return -1; // Отправлено не полностью
	}

	return 0; // Успех
}


// Получает полное протокольное сообщение из интерфейса
int receive_protocol_message(IOInterface *io, int handle, Message *message) {
    if (!io || !io->receive_data || handle < 0 || !message) {
        fprintf(stderr, "receive_protocol_message: Invalid arguments\n");
        return -1;
    }

    MessageHeader header_net; // Буфер для чтения заголовка в сетевом порядке
    ssize_t bytesRead;
    size_t totalBytesRead;

    // --- Этап 1: Чтение заголовка ---
    totalBytesRead = 0;
    while (totalBytesRead < sizeof(MessageHeader)) {
        // Используем функцию receive_data из интерфейса
        bytesRead = io->receive_data(handle, ((char*)&header_net) + totalBytesRead, sizeof(MessageHeader) - totalBytesRead);

        if (bytesRead < 0) {
            // Ошибка уже должна быть выведена в io->receive_data
            fprintf(stderr, "receive_protocol_message: Ошибка получения заголовка (io->receive_data вернул %zd)\n", bytesRead);
            return -1; // Ошибка
        } else if (bytesRead == 0) {
             // Не выводим сообщение здесь, оно должно быть в реализации receive_data
            // printf("receive_protocol_message: Соединение закрыто при чтении заголовка.\n");
            return 1; // Соединение закрыто
        }
        totalBytesRead += bytesRead;
    }

    // Копируем заголовок (он все еще в сетевом порядке)
    memcpy(&message->header, &header_net, sizeof(MessageHeader));

    // --- Этап 2: Преобразование длины тела и чтение тела ---
    uint16_t bodyLenNet = message->header.body_length; // Длина в сетевом порядке
    uint16_t bodyLenHost = ntohs(bodyLenNet);          // Преобразуем для использования

    if (bodyLenHost > MAX_MESSAGE_BODY_SIZE) {
        fprintf(stderr, "receive_protocol_message: Ошибка: Полученная длина тела (%u) превышает максимальный размер (%d).\n", bodyLenHost, MAX_MESSAGE_BODY_SIZE);
        // Очистим буфер сокета, чтобы попытаться восстановиться? Или просто вернуть ошибку.
        // Пока просто возвращаем ошибку.
        return -1; // Ошибка - неверная длина
    }

    // Читаем тело сообщения нужной длины (если она > 0)
    if (bodyLenHost > 0) {
        totalBytesRead = 0;
        while (totalBytesRead < bodyLenHost) {
            // Используем функцию receive_data из интерфейса
            bytesRead = io->receive_data(handle, message->body + totalBytesRead, bodyLenHost - totalBytesRead);

            if (bytesRead < 0) {
                fprintf(stderr, "receive_protocol_message: Ошибка получения тела сообщения (io->receive_data вернул %zd)\n", bytesRead);
                return -1; // Ошибка
            } else if (bytesRead == 0) {
                // printf("receive_protocol_message: Соединение закрыто при чтении тела.\n");
                return 1; // Соединение закрыто
            }
            totalBytesRead += bytesRead;
        }
    }
    // Тело прочитано. Заголовок все еще в сетевом порядке.

    // --- Этап 3: Преобразование всего сообщения в хост-порядок ---
    message->header.body_length = bodyLenNet; // Убедимся, что длина в сетевом порядке для функции
    message_to_host_byte_order(message); // Преобразуем заголовок (кроме body_length) и тело

    printf("Получено сообщение через %s: Тип=%u, Номер=%u, Длина тела=%u, Handle=%d\n",
           (io->type == IO_TYPE_ETHERNET) ? "Ethernet" : ((io->type == IO_TYPE_SERIAL) ? "Serial" : "Unknown"),
           message->header.message_type,
           get_full_message_number(&message->header),
           message->header.body_length, // body_length теперь в хост-порядке
           handle);

    return 0; // Успех
}