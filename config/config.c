/*
 * config/config.c
 * Описание: Реализация функции загрузки конфигурации из INI-файла.
 * (Версия для загрузки ВСЕХ настроек SVM, включая параметры имитации сбоев)
 */
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // Для strcpy, strcasecmp
#include <strings.h> // Для strcasecmp в некоторых системах (хотя string.h обычно достаточно)
#include <stdbool.h>
#include "ini.h"
// #include "../protocol/protocol_defs.h" // LogicalAddress уже в config.h через AppConfig -> SvmInstanceSettings

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
    int svm_id_set = -1; // Переменная для ID из секции settings_svm

    // Макросы для удобства
    #define MATCH_SECTION(s) strcasecmp(section, s) == 0
    #define MATCH_PARAM(n) strcasecmp(name, n) == 0

    // Сначала обрабатываем общие секции
    if (MATCH_SECTION("communication")) {
        if (MATCH_PARAM("interface_type")) {
            snprintf(pconfig->interface_type, sizeof(pconfig->interface_type), "%s", value);
        } else if (MATCH_PARAM("uvm_keepalive_timeout_sec")) {
            pconfig->uvm_keepalive_timeout_sec = atoi(value);
            if (pconfig->uvm_keepalive_timeout_sec <= 0) { // Валидация
                fprintf(stderr, "Warning: Invalid uvm_keepalive_timeout_sec value '%s'. Using default.\n", value);
                pconfig->uvm_keepalive_timeout_sec = 15; // Восстанавливаем дефолт, если он был изменен
            }
        }
        return 1; // Секция обработана
    } else if (MATCH_SECTION("ethernet_uvm_target")) {
        if (MATCH_PARAM("target_ip")) {
            snprintf(pconfig->uvm_ethernet_target.target_ip, sizeof(pconfig->uvm_ethernet_target.target_ip), "%s", value);
        } else if (MATCH_PARAM("port")) {
            // Этот порт в [ethernet_uvm_target] не используется для подключения UVM к SVM,
            // т.к. UVM использует порты из [settings_svmN].
            // Он мог бы использоваться, если бы SVM подключался к UVM.
            // Оставим для совместимости или будущих нужд.
            pconfig->uvm_ethernet_target.port = (uint16_t)atoi(value);
        }
        return 1; // Секция обработана
    } else if (MATCH_SECTION("serial")) {
        if (MATCH_PARAM("device")) {
            snprintf(pconfig->serial.device, sizeof(pconfig->serial.device), "%s", value);
        } else if (MATCH_PARAM("baud_rate")) {
            pconfig->serial.baud_rate = atoi(value);
        } else if (MATCH_PARAM("data_bits")) {
            pconfig->serial.data_bits = atoi(value);
        } else if (MATCH_PARAM("parity")) {
            snprintf(pconfig->serial.parity, sizeof(pconfig->serial.parity), "%s", value);
        } else if (MATCH_PARAM("stop_bits")) {
            pconfig->serial.stop_bits = atoi(value);
        }
        return 1; // Секция обработана
    }

    // Затем пытаемся распознать секцию [settings_svmN]
    int sscanf_res_set = sscanf(section, "settings_svm%d", &svm_id_set);
    if (sscanf_res_set == 1) {
        if (svm_id_set >= 0 && svm_id_set < MAX_SVM_INSTANCES) {
            // Устанавливаем флаг, что конфиг для этого ID был найден,
            // даже если будет прочитан только один параметр из этой секции.
            pconfig->svm_config_loaded[svm_id_set] = true;

            if (MATCH_PARAM("port")) {
                pconfig->svm_ethernet[svm_id_set].port = (uint16_t)atoi(value);
            } else if (MATCH_PARAM("lak")) {
                pconfig->svm_settings[svm_id_set].lak = (LogicalAddress)strtol(value, NULL, 0); // strtol для hex 0x...
            } else if (MATCH_PARAM("simulate_control_failure")) {
                pconfig->svm_settings[svm_id_set].simulate_control_failure = parse_ini_boolean(value);
            } else if (MATCH_PARAM("disconnect_after_messages")) {
                pconfig->svm_settings[svm_id_set].disconnect_after_messages = atoi(value);
            } else if (MATCH_PARAM("simulate_response_timeout")) {
                pconfig->svm_settings[svm_id_set].simulate_response_timeout = parse_ini_boolean(value);
            } else if (MATCH_PARAM("send_warning_on_confirm")) {
                pconfig->svm_settings[svm_id_set].send_warning_on_confirm = parse_ini_boolean(value);
            } else if (MATCH_PARAM("warning_tks")) {
                pconfig->svm_settings[svm_id_set].warning_tks = (uint8_t)atoi(value);
            }
            // else {
            //     printf("Config_handler: Unknown parameter '%s' in section [%s]\n", name, section);
            // }
            return 1; // Секция settings_svmN обработана (или параметр в ней)
        } else {
            // Невалидный svm_id_set (например, settings_svm5, если MAX_SVM_INSTANCES=4)
            fprintf(stderr, "Config_handler: Invalid SVM ID %d in section [%s]\n", svm_id_set, section);
            return 0; // Ошибка, остановить парсинг для этой строки
        }
    }

    // Если секция не была распознана ни как общая, ни как settings_svmN
    // Это может быть, например, старая секция [ethernet_svmN], которую мы теперь не обрабатываем.
    // printf("Config_handler: Unhandled section: [%s]\n", section);
    return 1; // Игнорируем неизвестные секции, но продолжаем парсинг файла
}

// Функция загрузки конфигурации
int load_config(const char *filename, AppConfig *config) {
    // 1. Установить значения по умолчанию
    strncpy(config->interface_type, "ethernet", sizeof(config->interface_type) -1);
    config->interface_type[sizeof(config->interface_type)-1] = '\0';

    config->num_svm_configs_found = 0; // Будет подсчитано после парсинга

    strncpy(config->uvm_ethernet_target.target_ip, "127.0.0.1", sizeof(config->uvm_ethernet_target.target_ip)-1);
    config->uvm_ethernet_target.target_ip[sizeof(config->uvm_ethernet_target.target_ip)-1] = '\0';
    config->uvm_ethernet_target.port = 0; // Этот порт UVM не использует для подключения К SVM

    strncpy(config->serial.device, "/dev/ttyS0", sizeof(config->serial.device)-1);
    config->serial.device[sizeof(config->serial.device)-1] = '\0';
    config->serial.baud_rate = 115200;
    config->serial.data_bits = 8;
    strncpy(config->serial.parity, "none", sizeof(config->serial.parity)-1);
    config->serial.parity[sizeof(config->serial.parity)-1] = '\0';
    config->serial.stop_bits = 1;

    config->uvm_keepalive_timeout_sec = 15; // Значение по умолчанию

    // Устанавливаем дефолты для всех слотов SVM
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        config->svm_ethernet[i].port = 0; // Будет перезаписан из config.ini или установлен дефолт ниже
        config->svm_settings[i].lak = 0;  // Аналогично
        config->svm_settings[i].simulate_control_failure = false;
        config->svm_settings[i].disconnect_after_messages = -1;
        config->svm_settings[i].simulate_response_timeout = false;
        config->svm_settings[i].send_warning_on_confirm = false;
        config->svm_settings[i].warning_tks = 1; // TKS по умолчанию, если send_warning_on_confirm=true
        config->svm_config_loaded[i] = false; // Сбрасываем флаг перед парсингом
    }

    // 2. Запустить парсер
    int parse_result = ini_parse(filename, config_handler, config);

    // 3. Обработать результат парсинга
    if (parse_result < 0) {
        if (parse_result == -1) { // Ошибка открытия файла
            fprintf(stderr, "Warning: Config file '%s' not found or cannot be opened. Using defaults for all SVMs.\n", filename);
            // num_svm_configs_found останется 0, и для всех SVM будут применены дефолты ниже
        } else { // Другая ошибка парсера (например, -2 для malloc)
             fprintf(stderr, "Error: Config file '%s' internal error (Code: %d).\n", filename, parse_result);
             return -1;
        }
    } else if (parse_result > 0) { // Ошибка синтаксиса в INI файле
         fprintf(stderr, "Error: Parse error in config file '%s' on line %d. Some settings might be default.\n", filename, parse_result);
         // Продолжаем, но некоторые настройки могут быть дефолтными
    } else {
        printf("Configuration parsed successfully from '%s'.\n", filename);
    }

    // 4. Финализация настроек SVM: подсчет найденных и установка дефолтов для недостающих/невалидных
    config->num_svm_configs_found = 0;
    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) {
        if (config->svm_config_loaded[i]) { // Если хотя бы один параметр для этого SVM был в файле
            config->num_svm_configs_found++;
            // Проверяем, был ли порт и LAK явно установлены, иначе ставим дефолт
            if (config->svm_ethernet[i].port == 0) { // Если порт не был в [settings_svmN]
                fprintf(stderr, "Warning: Port for SVM %d not found in [settings_svm%d]. Using default %d.\n", i, i, 8080 + i);
                config->svm_ethernet[i].port = 8080 + i;
            }
            if (config->svm_settings[i].lak == 0) { // Если LAK не был в [settings_svmN]
                fprintf(stderr, "Warning: LAK for SVM %d not found in [settings_svm%d]. Using default 0x%02X.\n", i, i, (0x08 + i));
                config->svm_settings[i].lak = (LogicalAddress)(0x08 + i);
            }
        } else {
             // Если для SVM ID i вообще не было секции [settings_svmN], применяем полные дефолты
             config->svm_ethernet[i].port = 8080 + i;
             config->svm_settings[i].lak = (LogicalAddress)(0x08 + i);
             // Остальные svm_settings[i] уже имеют дефолты из цикла выше
        }

        // Общая валидация для всех слотов (загруженных и дефолтных)
        if (config->svm_ethernet[i].port == 0 || config->svm_ethernet[i].port > 65535) {
             fprintf(stderr, "Warning: Invalid port %d for SVM %d after parsing/defaults. Resetting to default %d.\n", config->svm_ethernet[i].port, i, 8080 + i);
             config->svm_ethernet[i].port = 8080 + i;
        }
        if (config->svm_settings[i].lak == 0) { // LAK=0 не используется
             fprintf(stderr, "Warning: Invalid LAK %d for SVM %d after parsing/defaults. Resetting to default 0x%02X.\n", config->svm_settings[i].lak, i, (0x08 + i));
             config->svm_settings[i].lak = (LogicalAddress)(0x08 + i);
        }
        if (config->svm_settings[i].disconnect_after_messages == 0) {
             config->svm_settings[i].disconnect_after_messages = -1; // 0 бессмысленно
        }
    }
	if (config->num_svm_configs_found > 0) {
		printf("Found configurations for %d SVM instances in file.\n", config->num_svm_configs_found);
	} else if (parse_result == -1) { // Файл не найден
		printf("No config file found. Using defaults for %d SVM instances.\n", MAX_SVM_INSTANCES);
	} else { // Файл был, но секций settings_svmN не нашлось
		printf("No [settings_svmN] sections found. Using defaults for %d SVM instances.\n", MAX_SVM_INSTANCES);
	}

    // 5. Вывод итоговой конфигурации
    printf("--- Effective Configuration ---\n");
    printf("  interface_type = %s\n", config->interface_type);
    printf("  uvm_keepalive_timeout_sec = %d\n", config->uvm_keepalive_timeout_sec);
    printf("  UVM Target IP (for SVMs to connect to, if UVM were server): %s\n", config->uvm_ethernet_target.target_ip);
    // UVM является клиентом, поэтому target_ip из uvm_ethernet_target используется как IP машины с SVM
    // А порт для UVM-клиента берется из svm_ethernet[i].port

    for (int i = 0; i < MAX_SVM_INSTANCES; ++i) { // Показываем все слоты, даже если они дефолтные
         printf("  SVM %d: Port=%u, LAK=0x%02X (Config loaded: %s)\n",
                i,
                config->svm_ethernet[i].port,
                config->svm_settings[i].lak,
                config->svm_config_loaded[i] ? "Yes" : "No");
         // Параметры сбоев всегда выводятся, т.к. для них есть дефолты
         printf("    Simulate Control Failure: %s\n", config->svm_settings[i].simulate_control_failure ? "Yes" : "No");
         printf("    Disconnect After: %d messages\n", config->svm_settings[i].disconnect_after_messages);
         printf("    Simulate Response Timeout: %s\n", config->svm_settings[i].simulate_response_timeout ? "Yes" : "No");
         printf("    Send Warning on Confirm: %s (TKS: %u)\n", config->svm_settings[i].send_warning_on_confirm ? "Yes" : "No", config->svm_settings[i].warning_tks);
    }
    printf("-----------------------------\n");

    return 0; // Успех, даже если файл не найден (используются дефолты)
}