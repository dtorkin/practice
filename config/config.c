/*
 * config/config.c
 * Описание: Реализация функции загрузки конфигурации из INI-файла.
 * (Версия для загрузки ВСЕХ настроек SVM в массивы, использует snprintf)
 */
#include "config.h"      // Включаем наш заголовок
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>     // Для strcasecmp
#include <stdbool.h>     // Для bool
#include "ini.h"
#include "../protocol/protocol_defs.h" // Для LOGICAL_ADDRESS_*

// Обработчик для библиотеки inih
static int config_handler(void* user, const char* section, const char* name,
                          const char* value) {
    AppConfig* pconfig = (AppConfig*)user;
    int svm_id = -1;
    // printf("DEBUG_CONFIG: Section=[%s], Name=[%s], Value=[%s]\n", section, name, value);

    #define MATCH(s, n) strcasecmp(section, s) == 0 && strcasecmp(name, n) == 0
    // Макрос MATCH_STRNCPY больше не используется

    // Пытаемся распознать секции для SVM
    int sscanf_res_eth = sscanf(section, "ethernet_svm%d", &svm_id);
    if (sscanf_res_eth == 1) {
        // printf("DEBUG_CONFIG: Matched ethernet_svm%d\n", svm_id);
        if (svm_id >= 0 && svm_id < MAX_SVM_CONFIGS) {
             if (strcasecmp(name, "port") == 0) {
                 pconfig->svm_ethernet[svm_id].port = (uint16_t)atoi(value);
                 // Флаг svm_config_loaded устанавливается позже, в load_config
                 // printf("DEBUG_CONFIG: Set port for SVM %d to %s\n", svm_id, value);
             }
             // Можно добавить другие параметры для ethernet_svmX, если нужно
             return 1; // Обработали секцию SVM
        }
    } else {
        int sscanf_res_set = sscanf(section, "svm_settings_%d", &svm_id);
         if (sscanf_res_set == 1) {
             // printf("DEBUG_CONFIG: Matched svm_settings_%d\n", svm_id);
            if (svm_id >= 0 && svm_id < MAX_SVM_CONFIGS) {
                 if (strcasecmp(name, "lak") == 0) {
                      pconfig->svm_settings[svm_id].lak = (LogicalAddress)strtol(value, NULL, 0); // Используем strtol для поддержки hex/dec
                     // Флаг svm_config_loaded устанавливается позже, в load_config
                      // printf("DEBUG_CONFIG: Set LAK for SVM %d to %s\n", svm_id, value);
                 }
                  // Можно добавить другие параметры для svm_settings_X, если нужно
                 return 1; // Обработали секцию SVM
            }
        }
    }

    // Если это не секция SVM, обрабатываем остальные
    if (MATCH("communication", "interface_type")) {
        snprintf(pconfig->interface_type, sizeof(pconfig->interface_type), "%s", value);
    }
    // --- Настройки для UVM ---
    else if (MATCH("ethernet_uvm_target", "target_ip")) {
        snprintf(pconfig->uvm_ethernet_target.target_ip,
                 sizeof(pconfig->uvm_ethernet_target.target_ip),
                 "%s", value);
    } else if (MATCH("ethernet_uvm_target", "port")) {
        pconfig->uvm_ethernet_target.port = (uint16_t)atoi(value);
    }
    // --- Настройки Serial ---
    else if (MATCH("serial", "device")) {
        snprintf(pconfig->serial.device, sizeof(pconfig->serial.device), "%s", value);
    } else if (MATCH("serial", "baud_rate")) {
        pconfig->serial.baud_rate = atoi(value);
    } else if (MATCH("serial", "data_bits")) {
        pconfig->serial.data_bits = atoi(value);
    } else if (MATCH("serial", "parity")) {
        snprintf(pconfig->serial.parity, sizeof(pconfig->serial.parity), "%s", value);
    } else if (MATCH("serial", "stop_bits")) {
        pconfig->serial.stop_bits = atoi(value);
    }
    else {
        // Игнорируем неизвестные или уже обработанные секции SVM
        return 1;
    }
    return 1; // Успех
}

// Функция загрузки конфигурации (2 аргумента)
int load_config(const char *filename, AppConfig *config) {
    // 1. Установить значения по умолчанию
    strcpy(config->interface_type, "ethernet");
    config->num_svm_configs_found = 0; // Сбрасываем счетчик

    // Дефолты для UVM target
    strcpy(config->uvm_ethernet_target.target_ip, "127.0.0.1");
    config->uvm_ethernet_target.port = 8080;
    // Дефолты для serial
    strcpy(config->serial.device, "/dev/ttyS0");
    config->serial.baud_rate = 115200;
    config->serial.data_bits = 8;
    strcpy(config->serial.parity, "none");
    config->serial.stop_bits = 1;

    // Инициализируем массивы SVM "пустыми" значениями (0)
    // и флаг загрузки в false
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        config->svm_ethernet[i].port = 0; // 0 как признак "не загружено"
        config->svm_settings[i].lak = 0;  // 0 как признак "не загружено"
        config->svm_config_loaded[i] = false;
    }

    // 2. Запустить парсер
    int parse_result = ini_parse(filename, config_handler, config);

    // 3. Обработать результат парсинга
    if (parse_result < 0) {
        if (parse_result == -1) {
            fprintf(stderr, "Warning: Config file '%s' not found or cannot be opened. Using defaults (if any).\n", filename);
             // В этом случае num_svm_configs_found останется 0
        } else {
             fprintf(stderr, "Error: Config file '%s' error (Memory allocation failed? Code: %d).\n", filename, parse_result);
             return -1; // Ошибка парсинга (не строка)
        }
    } else if (parse_result > 0) {
         fprintf(stderr, "Error: Parse error in config file '%s' on line %d.\n", filename, parse_result);
         return parse_result; // Номер строки с ошибкой
    } else {
        printf("Configuration parsed successfully from '%s'.\n", filename);
    }

    // 4. Подсчитать, сколько конфигов SVM реально найдено и установить флаги
    config->num_svm_configs_found = 0;
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        // Считаем конфиг найденным, если для него был прочитан порт ИЛИ LAK
        // (и они не равны нашему "невозможному" значению 0)
        if (config->svm_ethernet[i].port != 0 || config->svm_settings[i].lak != 0) {
            config->num_svm_configs_found++;
            config->svm_config_loaded[i] = true; // Устанавливаем флаг здесь
        } else {
             config->svm_config_loaded[i] = false;
        }
    }
    printf("Found configurations for %d SVM instances.\n", config->num_svm_configs_found);

    // 5. Валидация и установка дефолтов для НЕ НАЙДЕННЫХ параметров SVM
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        // Проверяем только те слоты, для которых мы ожидаем найти конфиг
        // (Хотя можно и для всех MAX_SVM_CONFIGS, чтобы задать дефолты)
        // if (config->svm_config_loaded[i]) { // <-- Закомментируем, чтобы задать дефолты для всех

             // Если порт остался 0 после парсинга (т.е. не был в файле), используем дефолт
             if (config->svm_ethernet[i].port == 0) {
                 config->svm_ethernet[i].port = 8080 + i; // Пример дефолта
                 // Можно вывести warning, если нужно
                 // fprintf(stderr, "Warning: Port for SVM %d not found. Using default %d.\n", i, config->svm_ethernet[i].port);
             } else if (config->svm_ethernet[i].port > 65535) { // Валидация прочитанного
                 fprintf(stderr, "Warning: Invalid port %d for SVM %d. Using default %d.\n", config->svm_ethernet[i].port, i, 8080 + i);
                 config->svm_ethernet[i].port = 8080 + i;
             }

             // Если LAK остался 0 после парсинга, используем дефолт
             if (config->svm_settings[i].lak == 0) { // LAK=0 не используется
                 config->svm_settings[i].lak = (LogicalAddress)(0x08 + i); // Пример дефолта
                 // fprintf(stderr, "Warning: LAK for SVM %d not found. Using default 0x%02X.\n", i, config->svm_settings[i].lak);
             }
             // Валидация LAK > 255 не нужна, т.к. тип LogicalAddress (uint8_t) это ограничивает
        // } // <-- Конец if (config->svm_config_loaded[i])
    }
    // ... (другие валидации для общих настроек, UVM, serial) ...
     if (strcasecmp(config->interface_type, "ethernet") != 0 && strcasecmp(config->interface_type, "serial") != 0) {
         fprintf(stderr, "Warning: Invalid interface_type '%s'. Using default 'ethernet'.\n", config->interface_type);
         strcpy(config->interface_type, "ethernet");
     }
     if (config->uvm_ethernet_target.port == 0) { /*...*/ }

    // Вывод итоговой конфигурации
    printf("--- Effective Configuration ---\n");
    printf("  interface_type = %s\n", config->interface_type);
    printf("  UVM Target: %s:%d\n", config->uvm_ethernet_target.target_ip, config->uvm_ethernet_target.port);
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
         // Выводим даже если не загружено, чтобы видеть дефолты
         printf("  SVM %d: Port=%d, LAK=0x%02X (Loaded: %s)\n",
                i,
                config->svm_ethernet[i].port,
                config->svm_settings[i].lak,
                config->svm_config_loaded[i] ? "Yes" : "No (Defaults)");
    }
    printf("-----------------------------\n");

    return 0; // Успех
}