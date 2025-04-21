/*
 * config/config.c
 * ... includes ...
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>
#include "ini.h"
#include "../protocol/protocol_defs.h"

// Обработчик для библиотеки inih
static int config_handler(void* user, const char* section, const char* name,
                          const char* value) {
    AppConfig* pconfig = (AppConfig*)user;
    int svm_id = -1;
    // printf("DEBUG_CONFIG: Section=[%s], Name=[%s], Value=[%s]\n", section, name, value); // Можно оставить для отладки

    #define MATCH(s, n) strcasecmp(section, s) == 0 && strcasecmp(name, n) == 0
    #define MATCH_STRNCPY(dest, src, size) \
        strncpy(dest, src, size - 1); \
        dest[size - 1] = '\0'

    // Пытаемся распознать секции для SVM
    int sscanf_res_eth = sscanf(section, "ethernet_svm%d", &svm_id);
    if (sscanf_res_eth == 1) {
        // printf("DEBUG_CONFIG: Matched ethernet_svm%d\n", svm_id);
        if (svm_id >= 0 && svm_id < MAX_SVM_CONFIGS) {
             if (strcasecmp(name, "port") == 0) {
                 pconfig->svm_ethernet[svm_id].port = (uint16_t)atoi(value);
                 // pconfig->svm_config_loaded[svm_id] = true; // <-- УБРАТЬ УСТАНОВКУ ФЛАГА
                 // printf("DEBUG_CONFIG: Set port for SVM %d to %s\n", svm_id, value);
             }
             return 1;
        }
    } else {
        int sscanf_res_set = sscanf(section, "svm_settings_%d", &svm_id);
         if (sscanf_res_set == 1) {
             // printf("DEBUG_CONFIG: Matched svm_settings_%d\n", svm_id);
            if (svm_id >= 0 && svm_id < MAX_SVM_CONFIGS) {
                 if (strcasecmp(name, "lak") == 0) {
                      pconfig->svm_settings[svm_id].lak = (LogicalAddress)strtol(value, NULL, 0);
                      // pconfig->svm_config_loaded[svm_id] = true; // <-- УБРАТЬ УСТАНОВКУ ФЛАГА
                      // printf("DEBUG_CONFIG: Set LAK for SVM %d to %s\n", svm_id, value);
                 }
                 return 1;
            }
        }
    }

    // Если это не секция SVM, обрабатываем остальные
    // ... (остальной код обработчика без изменений) ...
     if (MATCH("communication", "interface_type")) { /*...*/ }
     else if (MATCH("ethernet_uvm_target", "target_ip")) { /*...*/ }
     else if (MATCH("ethernet_uvm_target", "port")) { /*...*/ }
     // ... serial ...
     else { return 1; }
    return 1;
}

// Функция загрузки конфигурации (2 аргумента)
int load_config(const char *filename, AppConfig *config) {
    // 1. Установить значения по умолчанию
    strcpy(config->interface_type, "ethernet");
    config->num_svm_configs_found = 0;
    strcpy(config->uvm_ethernet_target.target_ip, "127.0.0.1");
    config->uvm_ethernet_target.port = 8080;
    strcpy(config->serial.device, "/dev/ttyS0");
    /*...*/

    // Устанавливаем "невозможные" значения, чтобы отличить от дефолтов или прочитанных нулей
    // Либо можно оставить дефолты, но тогда проверка будет менее надежной
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        config->svm_ethernet[i].port = 0; // Используем 0 как признак "не загружено"
        config->svm_settings[i].lak = 0;  // Используем 0 как признак "не загружено"
        // config->svm_config_loaded[i] = false; // Флаг больше не используется
    }

    // 2. Запустить парсер
    int parse_result = ini_parse(filename, config_handler, config);

    // 3. Обработать результат парсинга
    if (parse_result < 0) {
        if (parse_result == -1) {
            fprintf(stderr, "Warning: Config file '%s' not found or cannot be opened. Using defaults (if any).\n", filename);
             // В этом случае num_svm_configs_found останется 0
        } else { /* ... */ return -1;}
    } else if (parse_result > 0) { /* ... */ return parse_result; }
    else {
        printf("Configuration parsed successfully from '%s'.\n", filename);
    }

    // 4. Подсчитать, сколько конфигов SVM реально найдено (проверяя прочитанные значения)
    config->num_svm_configs_found = 0;
    for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        // Считаем конфиг найденным, если для него был прочитан порт ИЛИ LAK
        // (и они не равны нашему "невозможному" значению 0)
        if (config->svm_ethernet[i].port != 0 || config->svm_settings[i].lak != 0) {
            config->num_svm_configs_found++;
            // Установим флаг здесь для удобства дальнейшей валидации/использования
            config->svm_config_loaded[i] = true;
        } else {
             config->svm_config_loaded[i] = false;
        }
    }
    printf("Found configurations for %d SVM instances.\n", config->num_svm_configs_found);

    // 5. Валидация (Теперь используем установленный флаг svm_config_loaded)
    // ... (код валидации без изменений, он уже использует svm_config_loaded) ...
     for (int i = 0; i < MAX_SVM_CONFIGS; ++i) {
        if (config->svm_config_loaded[i]) {
             // Если порт остался 0 после парсинга, значит он не был задан - используем дефолт
             if (config->svm_ethernet[i].port == 0 || config->svm_ethernet[i].port > 65535) {
                 uint16_t default_port = 8080 + i;
                 fprintf(stderr, "Warning: Invalid or missing port for SVM %d. Using default %d.\n", i, default_port);
                 config->svm_ethernet[i].port = default_port;
             }
             // Если LAK остался 0 после парсинга, используем дефолт
             if (config->svm_settings[i].lak == 0) { // LAK=0 не используется
                 LogicalAddress default_lak = (LogicalAddress)(0x08 + i);
                 fprintf(stderr, "Warning: Invalid or missing LAK for SVM %d. Using default 0x%02X.\n", i, default_lak);
                 config->svm_settings[i].lak = default_lak;
             }
        }
    }


    // Вывод итоговой конфигурации (без изменений)
    // ...

    return 0; // Успех
}