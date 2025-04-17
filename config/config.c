/*
 * config/config.c
 *
 * Описание:
 * Реализация функции загрузки конфигурации из INI-файла
 * с использованием библиотеки inih.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // <-- Добавлено для strcasecmp
#include "ini.h" // Подключаем заголовочный файл библиотеки inih

// Вспомогательная функция stricmp больше не нужна

// Обработчик для библиотеки inih
static int config_handler(void* user, const char* section, const char* name,
                          const char* value) {
    AppConfig* pconfig = (AppConfig*)user;

    #define MATCH(s, n) strcasecmp(section, s) == 0 && strcasecmp(name, n) == 0 // Используем strcasecmp для нечувствительности к регистру
    #define MATCH_STRNCPY(dest, src, size) \
        strncpy(dest, src, size - 1); \
        dest[size - 1] = '\0'

    if (MATCH("communication", "interface_type")) {
        MATCH_STRNCPY(pconfig->interface_type, value, sizeof(pconfig->interface_type));
    } else if (MATCH("ethernet", "target_ip")) {
        MATCH_STRNCPY(pconfig->ethernet.target_ip, value, sizeof(pconfig->ethernet.target_ip));
    } else if (MATCH("ethernet", "port")) {
        pconfig->ethernet.port = (uint16_t)atoi(value); // Приведение к uint16_t
    } else if (MATCH("serial", "device")) {
        MATCH_STRNCPY(pconfig->serial.device, value, sizeof(pconfig->serial.device));
    } else if (MATCH("serial", "baud_rate")) {
        pconfig->serial.baud_rate = atoi(value);
    } else if (MATCH("serial", "data_bits")) {
        pconfig->serial.data_bits = atoi(value);
    } else if (MATCH("serial", "parity")) {
        MATCH_STRNCPY(pconfig->serial.parity, value, sizeof(pconfig->serial.parity));
    } else if (MATCH("serial", "stop_bits")) {
        pconfig->serial.stop_bits = atoi(value);
    } else {
        // Неизвестная секция/имя или пустая строка игнорируются
        // fprintf(stderr, "Warning: Unknown config section/name: [%s] %s = %s\n", section, name, value);
        return 1; // Возвращаем 1, чтобы парсер продолжил работу
    }
    return 1; // Успех
}

// Функция загрузки конфигурации
int load_config(const char *filename, AppConfig *config) {
    // 1. Установить значения по умолчанию
    strcpy(config->interface_type, "ethernet");
    strcpy(config->ethernet.target_ip, "127.0.0.1"); // Дефолт для UVM
    config->ethernet.port = 8080;
    strcpy(config->serial.device, "/dev/ttyS0");
    config->serial.baud_rate = 115200;
    config->serial.data_bits = 8;
    strcpy(config->serial.parity, "none");
    config->serial.stop_bits = 1;

    // 2. Запустить парсер
    int parse_result = ini_parse(filename, config_handler, config);

    // 3. Обработать результат парсинга
    if (parse_result < 0) {
        if (parse_result == -1) {
            fprintf(stderr, "Warning: Config file '%s' not found or cannot be opened. Using default values.\n", filename);
            // Не считаем ошибкой, если файл не найден, используем дефолтные значения
        } else if (parse_result > 0) {
             fprintf(stderr, "Error: Parse error in config file '%s' on line %d.\n", filename, parse_result);
             return parse_result; // Возвращаем номер строки с ошибкой
        } else {
             fprintf(stderr, "Error: Unknown error parsing config file '%s'. Code: %d\n", filename, parse_result);
             return -1; // Другая ошибка парсинга
        }
    } else {
        printf("Конфигурация из файла '%s' загружена.\n", filename);
    }


	// Дополнительная валидация (пример)
	if (config->ethernet.port == 0) { // Убрали проверку > 65535
		fprintf(stderr, "Warning: Invalid ethernet port %d in config. Using default %d.\n", config->ethernet.port, 8080);
		config->ethernet.port = 8080;
	}
    if (strcasecmp(config->interface_type, "ethernet") != 0 && strcasecmp(config->interface_type, "serial") != 0) { // Используем strcasecmp
         fprintf(stderr, "Warning: Invalid interface_type '%s' in config. Using default 'ethernet'.\n", config->interface_type);
         strcpy(config->interface_type, "ethernet");
    }
     if (strcasecmp(config->interface_type, "serial") == 0) {
         if (strcasecmp(config->serial.parity, "none") != 0 &&
             strcasecmp(config->serial.parity, "even") != 0 &&
             strcasecmp(config->serial.parity, "odd") != 0) {
             fprintf(stderr, "Warning: Invalid serial parity '%s'. Using default 'none'.\n", config->serial.parity);
             strcpy(config->serial.parity, "none");
         }
         // Добавить валидацию для baud_rate, data_bits, stop_bits
     }

    printf("Итоговая конфигурация:\n");
    printf("  interface_type = %s\n", config->interface_type);
    if (strcasecmp(config->interface_type, "ethernet") == 0) {
        printf("  [ethernet]\n");
        printf("    target_ip = %s\n", config->ethernet.target_ip);
        printf("    port = %d\n", config->ethernet.port);
    } else {
        printf("  [serial]\n");
        printf("    device = %s\n", config->serial.device);
        printf("    baud_rate = %d\n", config->serial.baud_rate);
        printf("    data_bits = %d\n", config->serial.data_bits);
        printf("    parity = %s\n", config->serial.parity);
        printf("    stop_bits = %d\n", config->serial.stop_bits);
    }


    return 0; // Успех (даже если файл не найден и используются дефолты)
}