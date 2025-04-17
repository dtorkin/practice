/*
 * io/io_serial.c
 *
 * Описание:
 * Реализация функций интерфейса ввода-вывода (IOInterface) для
 * последовательного порта (COM/tty) с использованием termios.
 */

#include "io_interface.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>      // Для open() и флагов O_RDWR, O_NOCTTY, O_NDELAY
#include <termios.h>    // Для управления терминалом (COM-портом)
#include <errno.h>
#include <sys/ioctl.h>  // Для ioctl (опционально, для управления линиями)
#include <poll.h>       // Для блокирующего чтения с таймаутом (если нужно)


// Вспомогательная функция для установки скорости (baud rate)
static int set_baud_rate(struct termios *tty, int speed) {
    speed_t baud;
    switch (speed) {
        case 9600:   baud = B9600;   break;
        case 19200:  baud = B19200;  break;
        case 38400:  baud = B38400;  break;
        case 57600:  baud = B57600;  break;
        case 115200: baud = B115200; break;
        case 230400: baud = B230400; break;
        // Добавьте другие скорости при необходимости
        default:
            fprintf(stderr, "set_baud_rate: Unsupported baud rate: %d\n", speed);
            return -1;
    }
    cfsetospeed(tty, baud);
    cfsetispeed(tty, baud);
    return 0;
}

// Вспомогательная функция для установки четности, бит данных, стоп-бит
static int set_port_attributes(int fd, int baud_rate, int data_bits, const char* parity, int stop_bits) {
    struct termios tty;

    // Получаем текущие атрибуты
    if (tcgetattr(fd, &tty) != 0) {
        perror("set_port_attributes: tcgetattr failed");
        return -1;
    }

    // Устанавливаем скорость
    if (set_baud_rate(&tty, baud_rate) != 0) {
        return -1;
    }

    // --- Установка флагов управления (c_cflag) ---
    tty.c_cflag &= ~CSIZE; // Очищаем биты размера данных
    switch (data_bits) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        case 8: tty.c_cflag |= CS8; break;
        default:
            fprintf(stderr, "set_port_attributes: Unsupported data size: %d\n", data_bits);
            return -1;
    }

    if (stop_bits == 1) {
        tty.c_cflag &= ~CSTOPB; // 1 стоп-бит
    } else if (stop_bits == 2) {
        tty.c_cflag |= CSTOPB;  // 2 стоп-бита
    } else {
        fprintf(stderr, "set_port_attributes: Unsupported stop bits: %d\n", stop_bits);
        return -1;
    }

    if (strcasecmp(parity, "none") == 0) {
        tty.c_cflag &= ~PARENB; // Без четности
        tty.c_iflag &= ~INPCK;  // Отключить проверку четности на входе
    } else if (strcasecmp(parity, "even") == 0) {
        tty.c_cflag |= PARENB;  // Включить четность
        tty.c_cflag &= ~PARODD; // Четность (Even)
        tty.c_iflag |= INPCK;   // Включить проверку четности на входе
    } else if (strcasecmp(parity, "odd") == 0) {
        tty.c_cflag |= PARENB;  // Включить четность
        tty.c_cflag |= PARODD;  // Нечетность (Odd)
        tty.c_iflag |= INPCK;   // Включить проверку четности на входе
    } else {
        fprintf(stderr, "set_port_attributes: Unsupported parity: %s\n", parity);
        return -1;
    }

    // Важно: Устанавливаем CLOCAL и CREAD
    // CLOCAL: Игнорировать сигналы управления модемом (DCD)
    // CREAD: Разрешить прием символов
    tty.c_cflag |= (CLOCAL | CREAD);

    // --- Установка флагов ввода (c_iflag) ---
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Отключить программное управление потоком (XON/XOFF)
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL); // Отключить спец. обработку ввода

    // --- Установка флагов вывода (c_oflag) ---
    tty.c_oflag &= ~OPOST; // Отключить всю обработку вывода (raw output)
    tty.c_oflag &= ~ONLCR; // Не преобразовывать NL в CRNL на выводе

    // --- Установка локальных флагов (c_lflag) ---
    // Отключить канонический режим (ICANON), эхо (ECHO, ECHOE, ECHONL),
    // сигналы (ISIG), и спец. обработку (IEXTEN)
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG | IEXTEN);

    // --- Настройка управления чтением (c_cc) ---
    // Минимальное количество символов для чтения (VMIN) и таймаут (VTIME)
    // Устанавливаем неблокирующее чтение (VMIN=0, VTIME=0) - наша функция receive будет блокироваться с помощью poll/select
    // Или можно установить блокирующее чтение до N байт или таймаута:
    tty.c_cc[VMIN]  = 0; // Не ждать определенного количества байт
    tty.c_cc[VTIME] = 1; // Таймаут 0.1 секунды для чтения (можно настроить)

    // Применяем новые атрибуты
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("set_port_attributes: tcsetattr failed");
        return -1;
    }

    // Очистить буферы порта
    tcflush(fd, TCIOFLUSH);

    return 0;
}


// --- Прототипы статических функций реализации ---
static int serial_connect_listen(IOInterface *self); // Общая функция открытия/настройки
static int serial_accept(IOInterface *self, char *client_ip_buffer, size_t buffer_len, uint16_t *client_port); // Заглушка
static int serial_disconnect(IOInterface *self, int handle);
static ssize_t serial_send(int handle, const void *buffer, size_t length);
static ssize_t serial_receive(int handle, void *buffer, size_t length);
static void serial_destroy(IOInterface *self);

// --- Функция-фабрика ---
IOInterface* create_serial_interface(const SerialConfig *config) {
    if (!config) {
        fprintf(stderr, "create_serial_interface: NULL config provided\n");
        return NULL;
    }

    IOInterface *interface = (IOInterface*)malloc(sizeof(IOInterface));
    if (!interface) {
        perror("create_serial_interface: Failed to allocate memory for interface");
        return NULL;
    }

    SerialConfig *config_copy = (SerialConfig*)malloc(sizeof(SerialConfig));
    if (!config_copy) {
        perror("create_serial_interface: Failed to allocate memory for config copy");
        free(interface);
        return NULL;
    }
    memcpy(config_copy, config, sizeof(SerialConfig));
    config_copy->base.type = IO_TYPE_SERIAL;

    interface->type = IO_TYPE_SERIAL;
    interface->config = config_copy;
    interface->io_handle = -1;
    interface->internal_data = NULL; // Пока не используем

    // Устанавливаем указатели на функции реализации
    interface->connect = serial_connect_listen; // Одна функция для открытия
    interface->listen = serial_connect_listen;  // Одна функция для открытия
    interface->accept = serial_accept;        // Заглушка для COM
    interface->disconnect = serial_disconnect;
    interface->send_data = serial_send;
    interface->receive_data = serial_receive;
    interface->destroy = serial_destroy;

    return interface;
}

// --- Реализации функций интерфейса ---

// Общая функция для открытия и настройки порта
static int serial_connect_listen(IOInterface *self) {
    if (!self || self->type != IO_TYPE_SERIAL || !self->config) {
        fprintf(stderr, "serial_connect_listen: Invalid interface or config\n");
        return -1;
    }
    if (self->io_handle != -1) {
        serial_disconnect(self, self->io_handle); // Закрываем старый
    }

    SerialConfig *config = (SerialConfig*)self->config;

    printf("Serial: Opening port %s...\n", config->device);

    // Открываем порт
    // O_RDWR - чтение/запись
    // O_NOCTTY - не делать этот порт управляющим терминалом
    // O_NDELAY / O_NONBLOCK - Неблокирующий режим открытия и чтения/записи.
    // Мы будем использовать poll/select для управления блокировкой в receive.
    self->io_handle = open(config->device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (self->io_handle < 0) {
        perror("serial_connect_listen: Error opening serial port");
        fprintf(stderr, "Hint: Check permissions for %s (e.g., add user to 'dialout' group).\n", config->device);
        self->io_handle = -1;
        return -1;
    }

    // Убираем флаг O_NDELAY после открытия, чтобы read/write могли блокироваться (если VMIN/VTIME настроены)
    // или чтобы poll/select работали корректно.
     fcntl(self->io_handle, F_SETFL, 0);

    // Настраиваем атрибуты порта
    if (set_port_attributes(self->io_handle, config->baud_rate, config->data_bits, config->parity, config->stop_bits) != 0) {
        close(self->io_handle);
        self->io_handle = -1;
        return -1;
    }

    printf("Serial: Port %s opened and configured successfully (handle: %d)\n", config->device, self->io_handle);
    return self->io_handle;
}

// accept не применим к COM-порту
static int serial_accept(IOInterface *self, char *client_ip_buffer, size_t buffer_len, uint16_t *client_port) {
    (void)self;
    (void)client_ip_buffer;
    (void)buffer_len;
    (void)client_port;
    fprintf(stderr, "serial_accept: Operation not supported for serial interface\n");
    errno = EOPNOTSUPP; // Operation not supported
    return -1;
}

static int serial_disconnect(IOInterface *self, int handle) {
    (void)self; // self не используется напрямую
    if (handle < 0) {
        return -1; // Нечего закрывать
    }
    printf("Serial: Closing handle %d\n", handle);
    if (close(handle) < 0) {
        perror("serial_disconnect: close failed");
        return -1;
    }
     // Если закрывали основной дескриптор интерфейса, сбрасываем его
    if (self && self->io_handle == handle) {
        self->io_handle = -1;
    }
    return 0;
}

// Отправка данных в COM-порт
static ssize_t serial_send(int handle, const void *buffer, size_t length) {
     if (handle < 0 || buffer == NULL) {
        return -1;
    }
    ssize_t total_written = 0;
    ssize_t written_now;

    while (total_written < (ssize_t)length) {
        do {
            written_now = write(handle, (const char*)buffer + total_written, length - total_written);
        } while (written_now < 0 && errno == EINTR); // Повтор при прерывании сигналом

        if (written_now < 0) {
            perror("serial_send: write failed");
            return -1; // Ошибка записи
        }
        if (written_now == 0) {
             // Это может произойти, если порт настроен неблокирующим и буфер переполнен,
             // но с настройками по умолчанию write должен блокироваться или возвращать ошибку.
             fprintf(stderr, "serial_send: write returned 0 (unexpected)\n");
             // Можно добавить небольшую паузу и повторить попытку
             usleep(10000); // 10ms
             continue;
        }
        total_written += written_now;
    }

    // Убедиться, что все данные записаны перед возвратом (важно для serial)
    // tcdrain(handle); // Ждет, пока весь вывод будет передан. Может быть излишним.

    return total_written;
}

// Чтение данных из COM-порта
static ssize_t serial_receive(int handle, void *buffer, size_t length) {
    if (handle < 0 || buffer == NULL || length == 0) {
        return -1;
    }

    // Используем poll для ожидания данных, чтобы сделать чтение более управляемым,
    // особенно если VMIN=0, VTIME>0 (таймаут) или VMIN=0, VTIME=0 (неблокирующий).
    struct pollfd pfd;
    pfd.fd = handle;
    pfd.events = POLLIN; // Ждем доступности данных для чтения

    // Таймаут для poll (например, 1 секунда). Можно сделать настраиваемым.
    // -1 для бесконечного ожидания. 0 для неблокирующей проверки.
    int timeout_ms = 1000;

    int ret = poll(&pfd, 1, timeout_ms);

    if (ret < 0) {
        if (errno == EINTR) {
            return -2; // Сигнал прервал ожидание, вызывающий код может повторить
        }
        perror("serial_receive: poll failed");
        return -1; // Ошибка poll
    } else if (ret == 0) {
        // Таймаут истек, данных нет
        return -2; // Используем -2 для обозначения таймаута/нет данных сейчас
                   // или можно вернуть 0, но 0 обычно означает конец файла/закрытие.
    }

    // Данные доступны для чтения (pfd.revents & POLLIN)
    ssize_t bytes_read;
    do {
        bytes_read = read(handle, buffer, length);
    } while (bytes_read < 0 && errno == EINTR); // Повтор при прерывании

    if (bytes_read < 0) {
        perror("serial_receive: read failed");
        return -1; // Ошибка чтения
    }
    // bytes_read == 0 обычно означает EOF, что для COM-порта маловероятно,
    // если он не был закрыт. Обрабатываем как ошибку или 0.
    // bytes_read > 0 - успешное чтение

    return bytes_read;
}

// Освобождение ресурсов интерфейса Serial
static void serial_destroy(IOInterface *self) {
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
    printf("Serial Interface destroyed.\n");
}