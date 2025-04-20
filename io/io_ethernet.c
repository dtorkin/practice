/*
 * io/io_ethernet.c
 *
 * Описание:
 * Реализация функций интерфейса ввода-вывода (IOInterface) для Ethernet (TCP/IP).
 */

#include "io_interface.h" // Определения интерфейса и структур конфигурации
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// --- Прототипы статических функций реализации ---
static int ethernet_connect(IOInterface *self);
static int ethernet_listen(IOInterface *self);
static int ethernet_accept(IOInterface *self, char *client_ip_buffer, size_t buffer_len, uint16_t *client_port);
static int ethernet_disconnect(IOInterface *self, int handle);
static ssize_t ethernet_send(int handle, const void *buffer, size_t length);
static ssize_t ethernet_receive(int handle, void *buffer, size_t length);
static void ethernet_destroy(IOInterface *self);

// --- Функция-фабрика ---
IOInterface* create_ethernet_interface(const EthernetConfig *config) {
    if (!config) {
        fprintf(stderr, "create_ethernet_interface: NULL config provided\n");
        return NULL;
    }

    // Выделяем память под основную структуру интерфейса
    IOInterface *interface = (IOInterface*)malloc(sizeof(IOInterface));
    if (!interface) {
        perror("create_ethernet_interface: Failed to allocate memory for interface");
        return NULL;
    }

    // Выделяем память под копию конфигурации и копируем ее
    EthernetConfig *config_copy = (EthernetConfig*)malloc(sizeof(EthernetConfig));
    if (!config_copy) {
        perror("create_ethernet_interface: Failed to allocate memory for config copy");
        free(interface);
        return NULL;
    }
    memcpy(config_copy, config, sizeof(EthernetConfig));
    config_copy->base.type = IO_TYPE_ETHERNET; // Устанавливаем тип в копии

    // Инициализируем поля интерфейса
    interface->type = IO_TYPE_ETHERNET;
    interface->config = config_copy; // Сохраняем указатель на копию
    interface->io_handle = -1;       // Дескриптор пока не создан
    interface->internal_data = NULL; // Для Ethernet пока не нужно

    // Устанавливаем указатели на функции реализации
    interface->connect = ethernet_connect;
    interface->listen = ethernet_listen;
    interface->accept = ethernet_accept;
    interface->disconnect = ethernet_disconnect;
    interface->send_data = ethernet_send;
    interface->receive_data = ethernet_receive;
    interface->destroy = ethernet_destroy;

    return interface;
}

// --- Реализации функций интерфейса ---

static int ethernet_connect(IOInterface *self) {
    if (!self || self->type != IO_TYPE_ETHERNET || !self->config) {
        fprintf(stderr, "ethernet_connect: Invalid interface or config\n");
        return -1;
    }
    // Закрываем предыдущее соединение, если оно было
    if (self->io_handle != -1) {
        ethernet_disconnect(self, self->io_handle);
        self->io_handle = -1;
    }

    EthernetConfig *config = (EthernetConfig*)self->config;
    struct sockaddr_in server_addr;

    // Создаем сокет
    self->io_handle = socket(AF_INET, SOCK_STREAM, 0);
    if (self->io_handle < 0) {
        perror("ethernet_connect: Failed to create socket");
        self->io_handle = -1;
        return -1;
    }

    // Готовим адрес сервера
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(config->port);
	int pton_res = inet_pton(AF_INET, config->target_ip, &server_addr.sin_addr);
	if (pton_res == 0) {
		fprintf(stderr, "ethernet_connect: Invalid server address format: %s\n", config->target_ip);
		close(self->io_handle);
		self->io_handle = -1;
		return -1;
	} else if (pton_res < 0) {
		perror("ethernet_connect: inet_pton failed");
		close(self->io_handle);
		self->io_handle = -1;
		return -1;
	}

    // Подключаемся
    if (connect(self->io_handle, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("ethernet_connect: Connection failed");
        close(self->io_handle);
        self->io_handle = -1;
        return -1;
    }

    printf("Ethernet: Connected to %s:%d (handle: %d)\n", config->target_ip, config->port, self->io_handle);
    return self->io_handle; // Возвращаем дескриптор соединения
}

static int ethernet_listen(IOInterface *self) {
     if (!self || self->type != IO_TYPE_ETHERNET || !self->config) {
        fprintf(stderr, "ethernet_listen: Invalid interface or config\n");
        return -1;
    }
    if (self->io_handle != -1) {
        ethernet_disconnect(self, self->io_handle); // Закрываем старый, если был
    }

    EthernetConfig *config = (EthernetConfig*)self->config;
    struct sockaddr_in server_addr;

    // Создаем сокет
    self->io_handle = socket(AF_INET, SOCK_STREAM, 0);
    if (self->io_handle < 0) {
        perror("ethernet_listen: Failed to create socket");
        self->io_handle = -1;
        return -1;
    }

    // Разрешаем переиспользование адреса
    int opt = 1;
    if (setsockopt(self->io_handle, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("ethernet_listen: setsockopt(SO_REUSEADDR) failed");
        // Не фатально, но может мешать быстрому перезапуску
    }

    // Готовим адрес для прослушивания
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Всегда слушаем на всех локальных IP
    server_addr.sin_port = htons(config->port);

    // Привязываем сокет
    if (bind(self->io_handle, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("ethernet_listen: Bind failed");
        close(self->io_handle);
        self->io_handle = -1;
        return -1;
    }

    // Начинаем слушать
    if (listen(self->io_handle, 1) < 0) { // Очередь ожидания = 1
        perror("ethernet_listen: Listen failed");
        close(self->io_handle);
        self->io_handle = -1;
        return -1;
    }

    printf("Ethernet: Listening on port %d (handle: %d)\n", config->port, self->io_handle);
    return self->io_handle; // Возвращаем слушающий дескриптор
}

static int ethernet_accept(IOInterface *self, char *client_ip_buffer, size_t buffer_len, uint16_t *client_port) {
    if (!self || self->type != IO_TYPE_ETHERNET || self->io_handle < 0) {
         fprintf(stderr, "ethernet_accept: Invalid interface or not listening\n");
        return -1;
    }

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_handle = -1;

    // Принимаем соединение (блокирующий вызов)
    do {
        client_handle = accept(self->io_handle, (struct sockaddr *)&client_addr, &client_len);
    } while (client_handle < 0 && errno == EINTR); // Повторяем, если прервано сигналом

    if (client_handle < 0) {
        perror("ethernet_accept: Accept failed");
        return -1;
    }

    // Получаем IP и порт клиента, если буферы предоставлены
    if (client_ip_buffer && buffer_len > 0) {
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip_buffer, buffer_len);
        client_ip_buffer[buffer_len - 1] = '\0'; // Гарантируем нуль-терминацию
    }
    if (client_port) {
        *client_port = ntohs(client_addr.sin_port);
    }

    return client_handle; // Возвращаем дескриптор клиента
}


static int ethernet_disconnect(IOInterface *self, int handle) {
    (void)self; // self не используется напрямую, но нужен для сигнатуры
    if (handle < 0) {
        //fprintf(stderr, "ethernet_disconnect: Invalid handle: %d\n", handle);
        return -1; // Нечего закрывать
    }
    printf("Ethernet: Closing handle %d\n", handle);
    if (close(handle) < 0) {
        perror("ethernet_disconnect: close failed");
        return -1;
    }
    // Если закрывали основной дескриптор интерфейса, сбрасываем его
    if (self && self->io_handle == handle) {
        self->io_handle = -1;
    }
    return 0;
}

static ssize_t ethernet_send(int handle, const void *buffer, size_t length) {
     if (handle < 0 || buffer == NULL || length == 0) {
        return -1;
    }
    ssize_t total_sent = 0;
    while(total_sent < (ssize_t)length) {
        ssize_t sent_now = send(handle, (const char*)buffer + total_sent, length - total_sent, 0);
        if (sent_now < 0) {
            if (errno == EINTR) continue; // Повторить при прерывании
            perror("ethernet_send: send failed");
            return -1;
        }
        if (sent_now == 0) {
            // Сокет закрыт другой стороной? Маловероятно для TCP send.
            fprintf(stderr, "ethernet_send: send returned 0\n");
            return total_sent; // Вернуть то, что успели отправить
        }
        total_sent += sent_now;
    }
    return total_sent;
}

static ssize_t ethernet_receive(int handle, void *buffer, size_t length) {
    if (handle < 0 || buffer == NULL || length == 0) {
        return -1;
    }
    ssize_t bytes_received;
    do {
        bytes_received = recv(handle, buffer, length, 0);
    } while (bytes_received < 0 && errno == EINTR); // Повторить при прерывании

    if (bytes_received < 0) {
        perror("ethernet_receive: recv failed");
    }
    // bytes_received == 0 означает, что соединение закрыто удаленно
    // bytes_received > 0 означает успешное чтение
    return bytes_received;
}

static void ethernet_destroy(IOInterface *self) {
    if (!self) return;

    // Закрываем основной дескриптор, если он еще открыт
    if (self->io_handle >= 0) {
        close(self->io_handle);
        self->io_handle = -1;
    }
    // Освобождаем память, выделенную для конфигурации
    free(self->config);
    self->config = NULL;
    // Освобождаем память под внутренние данные (если бы они были)
    free(self->internal_data);
    self->internal_data = NULL;
    // Освобождаем память самой структуры интерфейса
    free(self);
     printf("Ethernet Interface destroyed.\n");
}