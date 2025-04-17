/*
 * io/io_interface.h
 *
 * Описание:
 * Определяет абстрактный интерфейс для операций ввода-вывода (I/O),
 * позволяя работать с различными типами соединений (Ethernet, Serial)
 * через единый набор функций.
 */

#ifndef IO_INTERFACE_H
#define IO_INTERFACE_H

#include <stddef.h> // для size_t
#include <sys/types.h> // для ssize_t
#include "../config/config.h" // Для AppConfig (или отдельных структур)

// Forward-декларация структуры интерфейса, чтобы избежать циклической зависимости,
// если бы мы включали конкретные io_ethernet.h/io_serial.h здесь (что мы не делаем).
struct IOInterface;

// Определяем тип для дескриптора. Может быть int (сокет, файл) или что-то еще.
typedef int io_handle_t;
#define INVALID_IO_HANDLE (-1) // Значение для невалидного дескриптора

// Структура для хранения информации о соединении (адрес клиента для сервера)
// Может понадобиться для логирования или управления несколькими клиентами в будущем.
typedef struct {
    char address_str[64]; // Строковое представление адреса (IP:port или имя порта)
    // Можно добавить другие поля при необходимости
} ConnectionInfo;


// Структура абстрактного интерфейса ввода-вывода
typedef struct IOInterface {
    // Указатель на данные конфигурации, специфичные для реализации.
    // Вызывающая сторона отвечает за выделение и освобождение памяти.
    // Это может быть EthernetConfig* или SerialConfig*.
    void *config;

    // Внутренний хендл/дескриптор, используемый реализацией.
    // Для клиента это может быть основной сокет/файл.
    // Для сервера это может быть слушающий сокет.
    io_handle_t handle;

    // Указатели на функции, реализующие операции ввода-вывода.

    /**
     * @brief Инициализирует интерфейс и устанавливает соединение (для клиента)
     *        или начинает прослушивание (для сервера).
     * @param self Указатель на структуру IOInterface.
     * @return 0 в случае успеха, -1 в случае ошибки. self->handle будет установлен при успехе.
     */
    int (*open)(struct IOInterface *self);

    /**
     * @brief Принимает входящее соединение (только для сервера).
     * @param self Указатель на структуру IOInterface (серверную).
     * @param client_info Указатель на структуру для информации о клиенте (может быть NULL).
     * @return Новый дескриптор для общения с клиентом (>= 0) или INVALID_IO_HANDLE в случае ошибки.
     */
    io_handle_t (*accept_conn)(struct IOInterface *self, ConnectionInfo *client_info);

    /**
     * @brief Закрывает конкретное соединение или слушающий сокет.
     * @param self Указатель на структуру IOInterface.
     * @param handle_to_close Дескриптор соединения для закрытия (от open() или accept_conn()).
     * @return 0 в случае успеха, -1 в случае ошибки.
     */
    int (*close_conn)(struct IOInterface *self, io_handle_t handle_to_close);

    /**
     * @brief Отправляет данные через указанный дескриптор.
     *        Функция должна пытаться отправить *все* указанные байты.
     * @param handle Дескриптор соединения.
     * @param buffer Указатель на буфер с данными.
     * @param length Количество байт для отправки.
     * @return Количество отправленных байт или -1 в случае ошибки.
     */
    ssize_t (*send_data)(io_handle_t handle, const void *buffer, size_t length);

    /**
     * @brief Получает данные из указанного дескриптора.
     *        Функция может быть блокирующей.
     * @param handle Дескриптор соединения.
     * @param buffer Указатель на буфер для приема данных.
     * @param max_length Максимальный размер буфера.
     * @return Количество полученных байт (> 0), 0 если соединение закрыто удаленно, -1 в случае ошибки.
     */
    ssize_t (*receive_data)(io_handle_t handle, void *buffer, size_t max_length);

    /**
     * @brief Освобождает ресурсы, связанные с интерфейсом (но не закрывает соединения).
     * Вызывается после завершения работы с интерфейсом.
     * @param self Указатель на структуру IOInterface.
     */
    void (*destroy)(struct IOInterface *self);

} IOInterface;


// --- Функции-фабрики (объявления) ---

/**
 * @brief Создает и инициализирует экземпляр IOInterface для Ethernet.
 * @param config Указатель на структуру с параметрами Ethernet. Память должна управляться извне.
 * @return Указатель на созданный IOInterface или NULL в случае ошибки.
 */
IOInterface* create_ethernet_interface(const EthernetConfig *config);

/**
 * @brief Создает и инициализирует экземпляр IOInterface для Serial port.
 * @param config Указатель на структуру с параметрами Serial. Память должна управляться извне.
 * @return Указатель на созданный IOInterface или NULL в случае ошибки.
 */
IOInterface* create_serial_interface(const SerialConfig *config);

#endif // IO_INTERFACE_H