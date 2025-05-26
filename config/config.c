/*
 * config/config.c
 * Описание: Реализация функции загрузки конфигурации из INI-файла.
 * (Версия для загрузки ВСЕХ настроек SVM, включая параметры имитации сбоев)
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include "ini.h"
#include "../protocol/protocol_defs.h"

// Вспомогательная функция для парсинга boolean значений из ini
static bool parse_ini_boolean(const char* value) {
    if (!value) return false;
    return (strcasecmp(value, "true") == 0 ||
            strcasecmp(value, "yes") == 0 ||
            strcasecmp(value, "on") == 0 ||
            strcmp(value, "1") == 0);
}

// Обработчик для библиотеки inih
static int config_handler(void* user, const char* section, const char* name,
                          const char* value) {
    AppConfig* pconfig = (AppConfig*)user;
    int svm_id = -1;

    #define MATCH(s, n) strcasecmp(section, s) == 0 && strcasecmp(name, n) == 0
    // snprintf используется вместо MATCH_STRNCPY

    // Пытаемся распознать секции для SVM
    int sscanf_res_eth = sscanf(section, "ethernet_svm%d", &svm_id);
    if (sscanf_res_eth == 1) {
        if (svm_id >= 0 && svm_id < MAX_SVM_INSTANCES) {
             if (strcasecmp(name, "port") == 0) {
                 pconfig->svm_ethernet[svm_id].port = (uint16_t)atoi(value);
             }
             // Другие параметры ethernet_svmX...
             return 1;
        }
    } else {
        int sscanf_res_set = sscanf(section, "settings_svm%d", &svm_id);
         if (sscanf_res_set == 1) {
            if (svm_id >= 0 && svm_id < MAX_SVM_INSTANCES) {
                 if (strcasecmp(name, "lak") == 0) {
                      pconfig->svm_settings[svm_id].lak = (LogicalAddress)strtol(value, NULL, 0);
                 } else if (strcasecmp(name, "simulate_control_failure") == 0) {
                      pconfig->svm_settings[svm_id].simulate_control_failure = parse_ini_boolean(value);
                 } else if (strcasecmp(name, "disconnect_after_messages") == 0) {
                      pconfig->svm_settings[svm_id].disconnect_after_messages = atoi(value);
                 } else if (strcasecmp(name, "simulate_response_timeout") == 0) {
                      pconfig->svm_settings[svm_id].simulate_response_timeout = parse_ini_boolean(value);
                 } else if (strcasecmp(name, "send_warning_on_confirm") == 0) {
                      pconfig->svm_settings[svm_id].send_warning_on_confirm = parse_ini_boolean(value);
                 } else if (strcasecmp(name, "warning_tks") == 0) {
                      pconfig->svm_settings[svm_id].warning_tks = (uint8_t)atoi(value);
                 } else if (MATCH("communication", "uvm_keepalive_timeout_sec")) { // Или другая подходящая секция, например [uvm_settings]
					  pconfig->uvm_keepalive_timeout_sec = atoi(value);
				 }
                 // Отмечаем, что конфиг для этого ID загружен, если ЛЮБОЙ параметр прочитан
                 pconfig->svm_config_loaded[svm_id] = true;
                 return 1;
            }
        }
    }

    // Если это не секция SVM, обрабатываем остальные
    if (MATCH("communication", "interface_type")) {
        snprintf(pconfig->interface_type, sizeof(pconfig->interface_type), "%s", value);
    }
    else if (MATCH("ethernet_uvm_target", "target_ip")) {
        snprintf(pconfig->uvm_ethernet_target.target_ip, sizeof(pconfig->uvm_ethernet_target.target_ip), "%s", value);
    } else if (MATCH("ethernet_uvm_target", "port")) {
        pconfig->uvm_ethernet_target.port = (uint16_t)atoi(value);
    }
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
    config->num_svm_configs_found = 0;
    strcpy(config->uvm_ethernet_target.target_ip, "127.0.0.1");
    config->uvm_ethernet_target.port = 8080;
    strcpy(config->serial.device, "/dev/ttyS0");
    config->serial.baud_rate = 115200;
    config->serial.data_bits = 8;
    strcpy(config->serial.parity, "none");
    config->serial.stop_bits = 1;
	config->uvm_keepalive_timeout_sec = 15;

    // Устанавливаем дефолты для всех слотов SVM
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        config->svm_ethernet[i].port = 0; // Признак "не загружено"
        config->svm_settings[i].lak = 0;  // Признак "не загружено"
        // Дефолты для имитации сбоев (все выключено)
        config->svm_settings[i].simulate_control_failure = false;
        config->svm_settings[i].disconnect_after_messages = -1; // Выключено
        config->svm_settings[i].simulate_response_timeout = false;
        config->svm_settings[i].send_warning_on_confirm = false;
        config->svm_settings[i].warning_tks = 1; // Пример TKS
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

    // 4. Подсчитать найденные конфиги SVM и установить дефолты, если не найдено
    config->num_svm_configs_found = 0;
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        // Проверяем флаг, установленный в config_handler
        if (config->svm_config_loaded[i]) {
            config->num_svm_configs_found++;
            // Если порт или LAK не были заданы в файле (остались 0), ставим дефолт
            if (config->svm_ethernet[i].port == 0) {
                config->svm_ethernet[i].port = 8080 + i;
            }
            if (config->svm_settings[i].lak == 0) {
                config->svm_settings[i].lak = (LogicalAddress)(0x08 + i);
            }
        } else {
             // Если конфиг для ID не найден, все равно установим дефолтные порт/LAK
             config->svm_ethernet[i].port = 8080 + i;
             config->svm_settings[i].lak = (LogicalAddress)(0x08 + i);
        }
    }
    printf("Found configurations for %d SVM instances in file.\n", config->num_svm_configs_found);

    // 5. Валидация (проверяем все слоты до MAX_SVM_INSTANCES, т.к. задали дефолты)
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
         // Валидация порта
         if (config->svm_ethernet[i].port == 0 || config->svm_ethernet[i].port > 65535) {
             fprintf(stderr, "Warning: Invalid port %d for SVM %d config slot. Using default %d.\n", config->svm_ethernet[i].port, i, 8080 + i);
             config->svm_ethernet[i].port = 8080 + i;
         }
         // Валидация LAK
         if (config->svm_settings[i].lak == 0) { // LAK=0 не используется
             fprintf(stderr, "Warning: Invalid LAK %d for SVM %d config slot. Using default 0x%02X.\n", config->svm_settings[i].lak, i, 0x08 + i);
             config->svm_settings[i].lak = (LogicalAddress)(0x08 + i);
         }
         // Валидация disconnect_after_messages
         if (config->svm_settings[i].disconnect_after_messages == 0) {
             // 0 бессмысленно, ставим -1 (выключено)
             config->svm_settings[i].disconnect_after_messages = -1;
         }
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
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
         printf("  SVM %d: Port=%d, LAK=0x%02X (Loaded: %s)\n",
                i,
                config->svm_ethernet[i].port,
                config->svm_settings[i].lak,
                config->svm_config_loaded[i] ? "Yes" : "No (Defaults)");
         // Вывод параметров сбоев
         if (config->svm_config_loaded[i]) { // Выводим только для найденных
            printf("    Simulate Control Failure: %s\n", config->svm_settings[i].simulate_control_failure ? "Yes" : "No");
            printf("    Disconnect After: %d messages\n", config->svm_settings[i].disconnect_after_messages);
            printf("    Simulate Response Timeout: %s\n", config->svm_settings[i].simulate_response_timeout ? "Yes" : "No");
            printf("    Send Warning on Confirm: %s (TKS: %u)\n", config->svm_settings[i].send_warning_on_confirm ? "Yes" : "No", config->svm_settings[i].warning_tks);
         }
    }
    printf("-----------------------------\n");

    return 0; // Успех
}