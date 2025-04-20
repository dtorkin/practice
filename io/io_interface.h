/*
 * io/io_interface.h
 *
 * Описание:
 * Определяет абстрактный интерфейс для операций ввода-вывода (I/O),
 * позволяя использовать различные транспортные механизмы (Ethernet, Serial).
 */

#ifndef IO_INTERFACE_H
#define IO_INTERFACE_H

#include <stddef.h>    // для size_t
#include <sys/types.h> // для ssize_t
#include <stdint.h>    // для uint16_t и т.д.

// --- Перечисление типов интерфейса ---
typedef enum {
    IO_TYPE_NONE,
    IO_TYPE_ETHERNET,
    IO_TYPE_SERIAL
} IOInterfaceType;

// --- Базовая структура конфигурации ---
typedef struct {
    IOInterfaceType type;
} IOConfigBase;

// --- Конфигурация для Ethernet ---
typedef struct {
    IOConfigBase base;
    char target_ip[40];
    uint16_t port;
	int base_port;
} EthernetConfig;

// --- Конфигурация для Serial ---
typedef struct {
    IOConfigBase base;
    char device[64];
    int baud_rate;
    int data_bits;
    char parity[8];
    int stop_bits;
} SerialConfig;

// --- Структура самого интерфейса ---
// !!!!! ПОЛНОЕ ОПРЕДЕЛЕНИЕ СТРУКТУРЫ ПЕРЕМЕЩЕНО СЮДА !!!!!
typedef struct IOInterface {
    // --- Данные ---
    IOInterfaceType type;         // Тип интерфейса
    void *config;               // Указатель на специфичную структуру конфигурации (EthernetConfig* или SerialConfig*)
    int io_handle;              // Основной дескриптор (сокет сервера или файл порта) (-1 если не инициализирован/закрыт)
    void *internal_data;        // Указатель на внутренние данные реализации (например, для хранения настроек termios)

    // --- Функции-члены (указатели на функции) ---
    int (*connect)(struct IOInterface *self);
    int (*listen)(struct IOInterface *self);
    int (*accept)(struct IOInterface *self, char *client_ip_buffer, size_t buffer_len, uint16_t *client_port);
    int (*disconnect)(struct IOInterface *self, int handle);
    ssize_t (*send_data)(int handle, const void *buffer, size_t length);
    ssize_t (*receive_data)(int handle, void *buffer, size_t length);
    void (*destroy)(struct IOInterface *self);

} IOInterface; // Теперь структура полностью определена здесь

// --- Функции-фабрики ---

/**
 * @brief Создает и инициализирует экземпляр Ethernet интерфейса.
 * Копирует конфигурацию.
 * @param config Указатель на конфигурацию Ethernet.
 * @return Указатель на созданный IOInterface или NULL в случае ошибки.
 */
IOInterface* create_ethernet_interface(const EthernetConfig *config);

/**
 * @brief Создает и инициализирует экземпляр Serial интерфейса.
 * Копирует конфигурацию.
 * @param config Указатель на конфигурацию Serial.
 * @return Указатель на созданный IOInterface или NULL в случае ошибки.
 */
IOInterface* create_serial_interface(const SerialConfig *config);

// Функция destroy вызывается через указатель в структуре.

#endif // IO_INTERFACE_H