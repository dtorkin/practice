/*
 * config/config.c
 * Описание: Реализация функции загрузки конфигурации из INI-файла.
 * (Версия для загрузки ВСЕХ настроек SVM в массивы)
 */
#include "config.h"      // Включаем наш заголовок
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "ini.h"
#include "../protocol/protocol_defs.h" // Для LOGICAL_ADDRESS_...

// Обработчик для библиотеки inih
static int config_handler(void* user, const char* section, const char* name,
                          const char* value) {
    AppConfig* pconfig = (AppConfig*)user;
    int svm_id = -1;
    printf("DEBUG_CONFIG: Section=[%s], Name=[%s], Value=[%s]\n", section, name, value); // <-- Добавь этот printf

    #define MATCH(s, n) strcasecmp(section, s) == 0 && strcasecmp(name, n) == 0
    #define MATCH_STRNCPY(dest, src, size) \
        strncpy(dest, src, size - 1); \
        dest[size - 1] = '\0'

    // Пытаемся распознать секции для SVM
    int sscanf_res_eth = sscanf(section, "ethernet_svm%d", &svm_id); // <-- Сохраним результат sscanf
    if (sscanf_res_eth == 1) {
        printf("DEBUG_CONFIG: Matched ethernet_svm%d\n", svm_id); // <-- Добавь этот printf
        if (svm_id >= 0 && svm_id < MAX_SVM_CONFIGS) {
             if (strcasecmp(name, "port") == 0) {
                 pconfig->svm_ethernet[svm_id].port = (uint16_t)atoi(value);
                 pconfig->svm_config_loaded[svm_id] = true;
                 printf("DEBUG_CONFIG: Set port for SVM %d to %s\n", svm_id, value); // <-- Добавь этот printf
             }
             return 1;
        }
    } else {
        int sscanf_res_set = sscanf(section, "svm_settings_%d", &svm_id); // <-- Сохраним результат sscanf
         if (sscanf_res_set == 1) {
             printf("DEBUG_CONFIG: Matched svm_settings_%d\n", svm_id); // <-- Добавь этот printf
            if (svm_id >= 0 && svm_id < MAX_SVM_CONFIGS) {
                 if (strcasecmp(name, "lak") == 0) {
                      pconfig->svm_settings[svm_id].lak = (LogicalAddress)strtol(value, NULL, 0);
                      pconfig->svm_config_loaded[svm_id] = true;
                      printf("DEBUG_CONFIG: Set LAK for SVM %d to %s\n", svm_id, value); // <-- Добавь этот printf
                 }
                 return 1;
            }
        }
    }

    // Если это не секция SVM, обрабатываем остальные
    if (MATCH("communication", "interface_type")) {
        MATCH_STRNCPY(pconfig->interface_type, value, sizeof(pconfig->interface_type));
    }
    // --- Настройки для UVM ---
    else if (MATCH("ethernet_uvm_target", "target_ip")) {
        // Сохраняем в uvm_ethernet_target
        MATCH_STRNCPY(pconfig->uvm_ethernet_target.target_ip, value, sizeof(pconfig->uvm_ethernet_target.target_ip));
    } else if (MATCH("ethernet_uvm_target", "port")) {
        // Сохраняем в uvm_ethernet_target
        pconfig->uvm_ethernet_target.port = (uint16_t)atoi(value);
    }
    // --- Настройки Serial ---
    else if (MATCH("serial", "device")) {
        MATCH_STRNCPY(pconfig->serial.device, value, sizeof(pconfig->serial.device));
    } else if (MATCH("serial", "baud_rate")) {
        pconfig->serial.baud_rate = atoi(value);
    } else if (MATCH("serial", "data_bits")) {
        pconfig->serial.data_bits = atoi(value);
    } else if (MATCH("serial", "parity")) {
        MATCH_STRNCPY(pconfig->serial.parity, value, sizeof(pconfig->serial.parity));
    } else if (MATCH("serial", "stop_bits")) {
        pconfig->serial.stop_bits = atoi(value);
    }
    // Удалены обработчики для старых полей ethernet, num_svm_instances, base_svm_lak
    else {
        // Игнорируем
        return 1;
    }
    return 1;
}

// Функция загрузки конфигурации (2 аргумента)
int load_config(const char *filename, AppConfig *config) {
    // 1. Установить значения по умолчанию
    strcpy(config->interface_type, "ethernet");
    config->num_svm_configs_found = 0;

    // Дефолты для UVM target
    strcpy(config->uvm_ethernet_target.target_ip, "127.0.0.1");
    config->uvm_ethernet_target.port = 8080;
    // Дефолты для serial
    strcpy(config->serial.device, "/dev/ttyS0");
    config->serial.baud_rate = 115200;
    config->serial.data_bits = 8;
    strcpy(config->serial.parity, "none");
    config->serial.stop_bits = 1;

    // Устанавливаем дефолты для всех возможных слотов SVM
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        config->svm_ethernet[i].port = 8080 + i;
        config->svm_settings[i].lak = (LogicalAddress)(0x08 + i); // Явное приведение типа
        config->svm_config_loaded[i] = false;
    }

    // 2. Запустить парсер
    int parse_result = ini_parse(filename, config_handler, config);

    // 3. Обработать результат парсинга (без изменений)
    if (parse_result < 0) { /*...*/ } else if (parse_result > 0) { /*...*/ } else { /*...*/ }

    // 4. Подсчитать найденные конфиги SVM (без изменений)
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) { /*...*/ }
    printf("Found configurations for %d SVM instances.\n", config->num_svm_configs_found);

    // 5. Валидация
    // Валидируем общие настройки
    if (strcasecmp(config->interface_type, "ethernet") != 0 && strcasecmp(config->interface_type, "serial") != 0) {
         fprintf(stderr, "Warning: Invalid interface_type '%s'. Using default 'ethernet'.\n", config->interface_type);
         strcpy(config->interface_type, "ethernet");
    }
    // Валидируем настройки Serial (если нужно)
    if (strcasecmp(config->interface_type, "serial") == 0) { /* ... */ }

    // Валидируем настройки UVM Target
    if (config->uvm_ethernet_target.port == 0) {
        fprintf(stderr, "Warning: Invalid uvm_ethernet_target port %d. Using default 8080.\n", config->uvm_ethernet_target.port);
		config->uvm_ethernet_target.port = 8080;
	}
    // TODO: Валидация uvm_ethernet_target.target_ip

    // Валидируем все найденные конфиги SVM
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        if (config->svm_config_loaded[i]) {
             if (config->svm_ethernet[i].port == 0 || config->svm_ethernet[i].port > 65535) {
                 fprintf(stderr, "Warning: Invalid port %d for SVM %d. Using default %d.\n", config->svm_ethernet[i].port, i, 8080 + i);
                 config->svm_ethernet[i].port = 8080 + i;
             }
             if (config->svm_settings[i].lak == 0) { // LAK=0 не используется
                 fprintf(stderr, "Warning: Invalid LAK %d for SVM %d. Using default %d.\n", config->svm_settings[i].lak, i, 0x08 + i);
                 config->svm_settings[i].lak = (LogicalAddress)(0x08 + i);
             }
        }
    }

    // Вывод итоговой конфигурации (без изменений)
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