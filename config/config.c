/*
 * config/config.c
 * Описание: Реализация функции загрузки конфигурации из INI-файла.
 * (Модифицировано для поддержки секций для разных SVM)
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h> // Для strcasecmp
#include "ini.h"

// Структура для передачи ID SVM в обработчик
typedef struct {
    AppConfig *config;
    int target_svm_id;
    char current_eth_section[32]; // "ethernet_svm0", "ethernet_svm1", ...
    char current_svm_section[32]; // "svm_settings_0", "svm_settings_1", ...
} ConfigParseCtx;


// Обработчик для библиотеки inih
static int config_handler(void* user, const char* section, const char* name,
                          const char* value) {
    ConfigParseCtx* ctx = (ConfigParseCtx*)user;
    AppConfig* pconfig = ctx->config;

    #define MATCH(s, n) strcasecmp(section, s) == 0 && strcasecmp(name, n) == 0
    #define MATCH_STRNCPY(dest, src, size) \
        strncpy(dest, src, size - 1); \
        dest[size - 1] = '\0'

    // --- Чтение общих настроек ---
    if (MATCH("communication", "interface_type")) {
        MATCH_STRNCPY(pconfig->interface_type, value, sizeof(pconfig->interface_type));
    }
    // --- Чтение настроек для КОНКРЕТНОГО SVM ID ---
    else if (strcmp(section, ctx->current_eth_section) == 0) { // Сравниваем с нужной секцией ethernet_svmX
         if (strcasecmp(name, "port") == 0) {
             pconfig->ethernet.port = (uint16_t)atoi(value);
         }
         // Можно добавить IP, если SVM должен слушать на определенном IP
         // else if (strcasecmp(name, "listen_ip") == 0) { ... }
    } else if (strcmp(section, ctx->current_svm_section) == 0) { // Сравниваем с нужной секцией svm_settings_X
         if (strcasecmp(name, "lak") == 0) {
              // Читаем LAK (может быть hex или dec)
              pconfig->svm_settings.lak = (LogicalAddress)strtol(value, NULL, 0);
         }
    }
    // --- Чтение настроек для UVM (оставлено для совместимости) ---
    else if (MATCH("ethernet_uvm_target", "target_ip")) {
        MATCH_STRNCPY(pconfig->uvm_ethernet_target.target_ip, value, sizeof(pconfig->uvm_ethernet_target.target_ip));
    } else if (MATCH("ethernet_uvm_target", "port")) {
        pconfig->uvm_ethernet_target.port = (uint16_t)atoi(value);
    } else if (MATCH("serial", "device")) {
        MATCH_STRNCPY(pconfig->serial.device, value, sizeof(pconfig->serial.device));
    } else if (MATCH("serial", "baud_rate")) {
        pconfig->serial.baud_rate = atoi(value);
    } // ... (остальные параметры serial) ...
    else {
        // Игнорируем неизвестные секции/имена или секции для других SVM ID
        return 1;
    }
    return 1; // Успех
}

// Функция загрузки конфигурации для конкретного SVM ID
int load_config(const char *filename, AppConfig *config, int svm_id) {
    // 1. Установить значения по умолчанию (общие и для SVM 0)
    strcpy(config->interface_type, "ethernet");
    // Дефолты для ethernet (будут переопределены для нужного ID)
    config->ethernet.port = 8080;
    // strcpy(config->ethernet.listen_ip, "0.0.0.0"); // Пример
    // Дефолты для настроек SVM (будут переопределены)
    config->svm_settings.lak = 0x08;
    // Дефолты для UVM target
    strcpy(config->uvm_ethernet_target.target_ip, "127.0.0.1");
    config->uvm_ethernet_target.port = 8080; // По умолчанию UVM подключается к порту SVM 0
    // Дефолты для serial
    strcpy(config->serial.device, "/dev/ttyS0");
    config->serial.baud_rate = 115200;
    config->serial.data_bits = 8;
    strcpy(config->serial.parity, "none");
    config->serial.stop_bits = 1;

    // 2. Подготовить контекст для парсера
    ConfigParseCtx ctx;
    ctx.config = config;
    ctx.target_svm_id = svm_id;
    // Формируем имена секций для целевого SVM ID
    snprintf(ctx.current_eth_section, sizeof(ctx.current_eth_section), "ethernet_svm%d", svm_id);
    snprintf(ctx.current_svm_section, sizeof(ctx.current_svm_section), "svm_settings_%d", svm_id);

    // 3. Запустить парсер
    int parse_result = ini_parse(filename, config_handler, &ctx);

    // 4. Обработать результат парсинга
    if (parse_result < 0) {
        if (parse_result == -1) {
            fprintf(stderr, "Warning: Config file '%s' not found or cannot be opened. Using default values.\n", filename);
        } else {
             fprintf(stderr, "Error: Config file '%s' error (Memory allocation failed? Code: %d).\n", filename, parse_result);
             return -1; // Ошибка парсинга (не строка)
        }
    } else if (parse_result > 0) {
         fprintf(stderr, "Error: Parse error in config file '%s' on line %d.\n", filename, parse_result);
         return parse_result; // Номер строки с ошибкой
    } else {
        printf("Configuration loaded successfully from '%s' for SVM ID %d.\n", filename, svm_id);
    }

    // 5. Валидация прочитанных значений (особенно для целевого SVM)
    if (config->ethernet.port == 0) {
        fprintf(stderr, "Warning: Invalid ethernet port %d for SVM %d. Using default %d.\n", config->ethernet.port, svm_id, 8080 + svm_id);
        config->ethernet.port = 8080 + svm_id; // Пример дефолта с ID
    }
    if (config->svm_settings.lak <= 0 || config->svm_settings.lak > 255) {
         fprintf(stderr, "Warning: Invalid LAK %d for SVM %d. Using default %d.\n", config->svm_settings.lak, svm_id, 0x08 + svm_id);
          config->svm_settings.lak = 0x08 + svm_id; // Пример дефолта с ID
    }
    // ... (другие валидации) ...

    // Вывод итоговой конфигурации (можно убрать или сделать опциональным)
    printf("--- Effective Configuration for SVM ID %d ---\n", svm_id);
    printf("  interface_type = %s\n", config->interface_type);
    printf("  [ethernet_svm%d]\n", svm_id);
    printf("    port = %d\n", config->ethernet.port);
    printf("  [svm_settings_%d]\n", svm_id);
    printf("    lak = 0x%02X\n", config->svm_settings.lak);
    printf("-------------------------------------------\n");

    return 0; // Успех
}