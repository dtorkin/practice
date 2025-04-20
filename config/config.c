/*
 * config/config.c
 * Описание: Реализация функции загрузки конфигурации из INI-файла.
 * (Модифицировано для загрузки ВСЕХ настроек SVM в массивы)
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ini.h"

// Обработчик для библиотеки inih
static int config_handler(void* user, const char* section, const char* name,
                          const char* value) {
    AppConfig* pconfig = (AppConfig*)user;
    int svm_id = -1; // Для определения индекса массива

    #define MATCH(s, n) strcasecmp(section, s) == 0 && strcasecmp(name, n) == 0
    #define MATCH_STRNCPY(dest, src, size) \
        strncpy(dest, src, size - 1); \
        dest[size - 1] = '\0'

    // Пытаемся распознать секции для SVM
    if (sscanf(section, "ethernet_svm%d", &svm_id) == 1) {
        if (svm_id >= 0 && svm_id < MAX_SVM_CONFIGS) {
             if (strcasecmp(name, "port") == 0) {
                 pconfig->svm_ethernet[svm_id].port = (uint16_t)atoi(value);
                 pconfig->svm_config_loaded[svm_id] = true; // Помечаем, что конфиг для этого ID есть
             }
             // else if (strcasecmp(name, "listen_ip") == 0) { ... }
             return 1; // Обработали секцию SVM
        }
    } else if (sscanf(section, "svm_settings_%d", &svm_id) == 1) {
        if (svm_id >= 0 && svm_id < MAX_SVM_CONFIGS) {
             if (strcasecmp(name, "lak") == 0) {
                  pconfig->svm_settings[svm_id].lak = (LogicalAddress)strtol(value, NULL, 0);
                  pconfig->svm_config_loaded[svm_id] = true;
             }
             return 1; // Обработали секцию SVM
        }
    }

    // Если это не секция SVM, обрабатываем остальные
    if (MATCH("communication", "interface_type")) {
        MATCH_STRNCPY(pconfig->interface_type, value, sizeof(pconfig->interface_type));
    } else if (MATCH("ethernet_uvm_target", "target_ip")) {
        MATCH_STRNCPY(pconfig->uvm_ethernet_target.target_ip, value, sizeof(pconfig->uvm_ethernet_target.target_ip));
    } else if (MATCH("ethernet_uvm_target", "port")) {
        pconfig->uvm_ethernet_target.port = (uint16_t)atoi(value);
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
        // Игнорируем другие неизвестные секции/имена
        return 1;
    }
    return 1; // Успех
}

// Функция загрузки конфигурации (2 аргумента)
int load_config(const char *filename, AppConfig *config) {
    // 1. Установить значения по умолчанию
    strcpy(config->interface_type, "ethernet");
    config->num_svm_configs_found = 0; // Сбрасываем счетчик найденных конфигов SVM

    // Дефолты для UVM target
    strcpy(config->uvm_ethernet_target.target_ip, "127.0.0.1");
    config->uvm_ethernet_target.port = 8080;
    // Дефолты для serial
    strcpy(config->serial.device, "/dev/ttyS0");
    config->serial.baud_rate = 115200; /*...*/

    // Устанавливаем дефолты для всех возможных слотов SVM
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        config->svm_ethernet[i].port = 8080 + i; // Пример дефолта порта
        config->svm_settings[i].lak = 0x08 + i;  // Пример дефолта LAK
        config->svm_config_loaded[i] = false;   // Изначально конфиг не загружен
    }

    // 2. Запустить парсер
    int parse_result = ini_parse(filename, config_handler, config);

    // 3. Обработать результат парсинга
    if (parse_result < 0) { /*...*/ } // Обработка ошибок как раньше
    else if (parse_result > 0) { /*...*/ }
    else {
        printf("Configuration loaded successfully from '%s'.\n", filename);
    }

    // 4. Подсчитать, сколько конфигов SVM реально найдено
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        if (config->svm_config_loaded[i]) {
            config->num_svm_configs_found++;
        }
    }
     printf("Found configurations for %d SVM instances.\n", config->num_svm_configs_found);


    // 5. Валидация (можно валидировать все найденные конфиги SVM)
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        if (config->svm_config_loaded[i]) { // Валидируем только загруженные
             if (config->svm_ethernet[i].port == 0) { /* ... */ }
             if (config->svm_settings[i].lak <= 0 || config->svm_settings[i].lak > 255) { /* ... */ }
        }
    }
    // ... (другие валидации для общих настроек и UVM) ...

    // Вывод итоговой конфигурации (можно сделать более подробным)
    printf("--- Effective Configuration ---\n");
    printf("  interface_type = %s\n", config->interface_type);
    printf("  UVM Target: %s:%d\n", config->uvm_ethernet_target.target_ip, config->uvm_ethernet_target.port);
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
         if (config->svm_config_loaded[i]) {
             printf("  SVM %d: Port=%d, LAK=0x%02X\n", i, config->svm_ethernet[i].port, config->svm_settings[i].lak);
         }
    }
    printf("-----------------------------\n");

    return 0; // Успех
}