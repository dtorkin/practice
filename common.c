/*
 * common.c
 *
 * Описание:
 * Содержит общие функции, которые (пока) не вынесены в другие модули.
 */

#include "common.h"
#include "protocol/message_utils.h" // Нужно для message_to_network_byte_order
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>     // Для write/send
#include <sys/socket.h> // Для send

// Отправить сообщение через сокет (будет перемещено в io/io_common.c)
int send_message(int socketFD, Message *message) {
	// Преобразуем в сетевой порядок перед отправкой
	message_to_network_byte_order(message);

	// Получаем актуальную длину тела в сетевом порядке
	uint16_t body_length_net = message->header.body_length;
    uint16_t body_length_host = ntohs(body_length_net);

    // Вычисляем общий размер сообщения для отправки
    size_t total_message_size = sizeof(MessageHeader) + body_length_host;

	ssize_t bytes_sent = send(socketFD, message, total_message_size, 0);

	// Важно: Преобразуем длину тела обратно в хост-порядок
    // после отправки, чтобы не влиять на дальнейшее использование
    // структуры Message в вызывающем коде.
    message->header.body_length = htons(body_length_host); // Back to network order temporarily
    message_to_host_byte_order(message); // Convert back to host order completely

	if (bytes_sent < 0) {
		perror("Ошибка отправки сообщения");
		return -1;
	} else if ((size_t)bytes_sent != total_message_size) {
        fprintf(stderr, "Ошибка отправки: отправлено %zd байт вместо %zu\n", bytes_sent, total_message_size);
        return -1; // Отправлено не полностью
    }
	return 0; // Успех
}