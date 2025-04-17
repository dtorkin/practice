/*
 * io/io_common.c
 *
 * Описание:
 * Реализация общих функций для отправки и приема сообщений протокола.
 */

#include "io_common.h"
#include "../protocol/message_utils.h" // Для преобразования порядка байт
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h> // Для send/recv
#include <arpa/inet.h>  // Для ntohs/htons

// Отправить сообщение через сокет
int send_message(int handle, Message *message) {
	// Преобразуем в сетевой порядок перед отправкой
	message_to_network_byte_order(message);

	// Получаем актуальную длину тела в *сетевом* порядке для заголовка
	uint16_t body_length_net = message->header.body_length;
	// Получаем длину тела в *хостовом* порядке для вычисления общего размера
    uint16_t body_length_host = ntohs(body_length_net);

    // Вычисляем общий размер сообщения для отправки
    size_t total_message_size = sizeof(MessageHeader) + body_length_host;

    printf("Отправка сообщения: Тип=%u, Номер=%u, Длина тела=%u, Общий размер=%zu\n",
           message->header.message_type,
           get_full_message_number(&message->header), // Используем утилиту
           body_length_host,
           total_message_size);


	ssize_t bytes_sent_total = 0;
    while(bytes_sent_total < (ssize_t)total_message_size) {
        ssize_t bytes_sent_now = send(handle, ((char*)message) + bytes_sent_total, total_message_size - bytes_sent_total, 0);

        if (bytes_sent_now < 0) {
            // Проверяем на прерывание сигналом
            if (errno == EINTR) {
                continue; // Повторяем отправку
            }
            perror("Ошибка отправки сообщения");
            // Важно: Преобразуем обратно перед выходом в случае ошибки
            message_to_host_byte_order(message);
            return -1;
        } else if (bytes_sent_now == 0) {
            // Это не должно происходить для блокирующих сокетов, но на всякий случай
             fprintf(stderr, "Ошибка отправки: send вернул 0\n");
             message_to_host_byte_order(message);
             return -1;
        }
        bytes_sent_total += bytes_sent_now;
    }


	// Преобразуем обратно в хост-порядок после успешной отправки,
    // чтобы структура оставалась в предсказуемом состоянии.
    message_to_host_byte_order(message);

	if ((size_t)bytes_sent_total != total_message_size) {
		fprintf(stderr, "Ошибка отправки: отправлено %zd байт вместо %zu\n", bytes_sent_total, total_message_size);
		return -1; // Отправлено не полностью (хотя цикл выше должен это предотвратить)
	}

	return 0; // Успех
}


// Получает полное сообщение (заголовок + тело) из указанного дескриптора.
int receive_full_message(int handle, Message *message) {
    MessageHeader header;
    ssize_t bytesRead;
    size_t totalBytesRead;

    // --- Этап 1: Чтение заголовка ---
    totalBytesRead = 0;
    while (totalBytesRead < sizeof(MessageHeader)) {
        do {
            bytesRead = recv(handle, ((char*)&header) + totalBytesRead, sizeof(MessageHeader) - totalBytesRead, 0);
        } while (bytesRead < 0 && errno == EINTR); // Повторить при прерывании сигналом

        if (bytesRead < 0) {
            perror("Ошибка получения заголовка");
            return -1; // Ошибка
        } else if (bytesRead == 0) {
            printf("receive_full_message: Соединение закрыто удаленной стороной при чтении заголовка.\n");
            return 1; // Соединение закрыто
        }
        totalBytesRead += bytesRead;
    }

    // Копируем заголовок в основную структуру
    memcpy(&message->header, &header, sizeof(MessageHeader));

    // --- Этап 2: Преобразование длины тела и чтение тела ---
    uint16_t bodyLenNet = message->header.body_length; // Длина в сетевом порядке
    uint16_t bodyLenHost = ntohs(bodyLenNet); // Преобразуем для использования

    if (bodyLenHost > MAX_MESSAGE_BODY_SIZE) {
        fprintf(stderr, "Ошибка: Полученная длина тела (%u) превышает максимальный размер (%d).\n", bodyLenHost, MAX_MESSAGE_BODY_SIZE);
        return -1; // Ошибка - неверная длина
    }

    // Читаем тело сообщения нужной длины (если она > 0)
    if (bodyLenHost > 0) {
        totalBytesRead = 0;
        while (totalBytesRead < bodyLenHost) {
            do {
                bytesRead = recv(handle, message->body + totalBytesRead, bodyLenHost - totalBytesRead, 0);
            } while (bytesRead < 0 && errno == EINTR); // Повторить при прерывании

            if (bytesRead < 0) {
                perror("Ошибка получения тела сообщения");
                return -1; // Ошибка
            } else if (bytesRead == 0) {
                printf("receive_full_message: Соединение закрыто удаленной стороной при чтении тела.\n");
                return 1; // Соединение закрыто
            }
            totalBytesRead += bytesRead;
        }
    }
     // На данный момент тело прочитано, но заголовок еще в сетевом порядке, кроме body_length, которое мы преобразовали.

    // --- Этап 3: Преобразование остальных полей ---
    message->header.body_length = bodyLenNet; // Восстанавливаем сетевой порядок для message_to_host_byte_order
    message_to_host_byte_order(message); // Преобразуем остальные поля в хост-порядок

    printf("Получено сообщение: Тип=%u, Номер=%u, Длина тела=%u\n",
        message->header.message_type,
        get_full_message_number(&message->header),
        message->header.body_length); // body_length теперь в хост-порядке

    return 0; // Успех
}