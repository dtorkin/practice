/*
 * config/config.h
 *
 * Описание:
 * Определяет структуру для хранения конфигурации приложения
 * и объявляет функцию для загрузки конфигурации из файла.
 */

#ifndef CONFIG_H
#define CONFIG_H

// Структура для Ethernet параметров
typedef struct {
    char target_ip[40]; // IP адрес SVM (для UVM) или IP для прослушивания (для SVM)
    int port;           // Порт
} EthernetConfig;

// Структура для Serial (COM-порт) параметров
typedef struct {
    char device[64];    // Имя устройства (e.g., /dev/ttyS0, COM1)
    int baud_rate;      // Скорость
    int data_bits;      // Биты данных (обычно 8)
    char parity[8];     // Четность ("none", "even", "odd")
    int stop_bits;      // Стоповые биты (1 или 2)
} SerialConfig;

// Основная структура конфигурации
typedef struct {
    // Секция [communication]
    char interface_type[16]; // "ethernet" или "serial"

    // Секция [ethernet]
    EthernetConfig ethernet;

    // Секция [serial]
    SerialConfig serial;

    // Можно добавить другие секции и параметры по мере необходимости

} AppConfig;

/**
 * @brief Загружает конфигурацию из INI-файла.
 *
 * Заполняет структуру AppConfig значениями из файла.
 * Устанавливает значения по умолчанию, если файл или параметры отсутствуют.
 *
 * @param filename Имя конфигурационного файла.
 * @param config Указатель на структуру AppConfig для заполнения.
 * @return 0 в случае успеха, -1 если файл не найден, >0 если ошибка парсинга (код ошибки inih).
 */
int load_config(const char *filename, AppConfig *config);

#endif // CONFIG_H