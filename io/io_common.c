/*
 * io/io_common.c
 *
 * Описание:
 * Реализация общих функций для отправки и приема протокольных сообщений
 * через абстрактный интерфейс IOInterface.
 */

#include "io_common.h"
#include "../protocol/message_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>  // <-- ДОБАВЛЕНО для ntohs

// Отправить протокольное сообщение через интерфейс
int send_protocol_message(IOInterface *io, int handle, Message *message) {
    if (!io || !io->send_data || handle < 0 || !message) {
        fprintf(stderr, "send_protocol_message: Invalid arguments\n");
        return -1;
    }

	message_to_network_byte_order(message);

	uint16_t body_length_net = message->header.body_length;
    uint16_t body_length_host = ntohs(body_length_net); // Теперь ntohs известен

    size_t total_message_size = sizeof(MessageHeader) + body_length_host;

    printf("Отправка сообщения через %s: Тип=%u, Номер=%u, Длина тела=%u, Общий размер=%zu, Handle=%d\n",
           (io->type == IO_TYPE_ETHERNET) ? "Ethernet" : ((io->type == IO_TYPE_SERIAL) ? "Serial" : "Unknown"),
           message->header.message_type,
           get_full_message_number(&message->header),
           body_length_host,
           total_message_size,
           handle);

    ssize_t bytes_sent = io->send_data(handle, message, total_message_size);

    message->header.body_length = body_length_net;
    message_to_host_byte_order(message);

    if (bytes_sent < 0) {
        fprintf(stderr, "send_protocol_message: io->send_data failed\n");
        return -1;
    } else if ((size_t)bytes_sent != total_message_size) {
		fprintf(stderr, "send_protocol_message: Ошибка отправки: отправлено %zd байт вместо %zu\n", bytes_sent, total_message_size);
		return -1;
	}

	return 0;
}


// Получает полное протокольное сообщение из интерфейса
int receive_protocol_message(IOInterface *io, int handle, Message *message) {
     if (!io || !io->receive_data || handle < 0 || !message) {
        fprintf(stderr, "receive_protocol_message: Invalid arguments\n");
        return -1;
    }

    MessageHeader header_net;
    ssize_t bytesRead;
    size_t totalBytesRead;

    // Этап 1: Чтение заголовка
    totalBytesRead = 0;
    while (totalBytesRead < sizeof(MessageHeader)) {
        bytesRead = io->receive_data(handle, ((char*)&header_net) + totalBytesRead, sizeof(MessageHeader) - totalBytesRead);
        if (bytesRead < 0) {
             // Проверяем на EINTR, который может вернуть serial_receive при таймауте poll
             if (errno == EINTR || bytesRead == -2) { // -2 - наш условный код для таймаута/нет данных
                 errno = 0; // Сбрасываем errno
                 usleep(10000); // Небольшая пауза перед повторной попыткой
                 continue;
             }
            fprintf(stderr, "receive_protocol_message: Ошибка получения заголовка (io->receive_data вернул %zd, errno %d)\n", bytesRead, errno);
            return -1;
        } else if (bytesRead == 0) {
            printf("receive_protocol_message: Соединение закрыто при чтении заголовка.\n");
            return 1;
        }
        totalBytesRead += bytesRead;
    }

    memcpy(&message->header, &header_net, sizeof(MessageHeader));

    // Этап 2: Преобразование длины и чтение тела
	printf("DEBUG RECV: header_net.body_length (network order) = 0x%04X (%u)\n", header_net.body_length, header_net.body_length);
	memcpy(&message->header, &header_net, sizeof(MessageHeader));
	uint16_t bodyLenNet = message->header.body_length;
	uint16_t bodyLenHost = ntohs(bodyLenNet);
	printf("DEBUG RECV: bodyLenHost (host order) = %u\n", bodyLenHost);

    if (bodyLenHost > MAX_MESSAGE_BODY_SIZE) {
        fprintf(stderr, "receive_protocol_message: Ошибка: Полученная длина тела (%u) > MAX (%d).\n", bodyLenHost, MAX_MESSAGE_BODY_SIZE);
        return -1;
    }

    if (bodyLenHost > 0) {
        totalBytesRead = 0;
        while (totalBytesRead < bodyLenHost) {
             bytesRead = io->receive_data(handle, message->body + totalBytesRead, bodyLenHost - totalBytesRead);
             if (bytesRead < 0) {
                 if (errno == EINTR || bytesRead == -2) {
                     errno = 0;
                     usleep(10000);
                     continue;
                 }
                fprintf(stderr, "receive_protocol_message: Ошибка получения тела сообщения (io->receive_data вернул %zd, errno %d)\n", bytesRead, errno);
                return -1;
             } else if (bytesRead == 0) {
                 printf("receive_protocol_message: Соединение закрыто при чтении тела.\n");
                 return 1;
             }
            totalBytesRead += bytesRead;
        }
    }

    // Этап 3: Преобразование всего сообщения в хост-порядок
	message->header.body_length = bodyLenNet; // ВОССТАНАВЛИВАЕМ СЕТЕВОЙ ПОРЯДОК ДЛЯ body_length
	message_to_host_byte_order(message); // Теперь эта функция получит body_length в сетевом порядке
	printf("DEBUG RECV: message.header.body_length после message_to_host_byte_order (должен быть хост) = %u\n", message->header.body_length);

    printf("Получено сообщение через %s: Тип=%u, Номер=%u, Длина тела=%u, Handle=%d\n",
           (io->type == IO_TYPE_ETHERNET) ? "Ethernet" : ((io->type == IO_TYPE_SERIAL) ? "Serial" : "Unknown"),
           message->header.message_type,
           get_full_message_number(&message->header),
           message->header.body_length,
           handle);

    return 0;
}