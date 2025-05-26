/*
 * config/config.h
 * Описание: Определяет структуру для хранения конфигурации приложения.
 * (Версия для загрузки ВСЕХ настроек SVM, включая параметры имитации сбоев)
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "../io/io_interface.h"
#include "../protocol/protocol_defs.h"
#include <stdbool.h> // <-- Добавляем для bool

// Максимальное количество SVM, чьи настройки можно хранить и эмулировать
#define MAX_SVM_INSTANCES 4

// Настройки, специфичные для одного SVM
typedef struct {
    LogicalAddress lak;
    // --- Параметры имитации сбоев ---
    bool simulate_control_failure; // Имитировать ошибку контроля (не ОК в RSK)?
    int disconnect_after_messages; // Отключиться после N исходящих сообщений (-1 = выкл)
    bool simulate_response_timeout; // Имитировать задержку ответа (для таймаута UVM)?
    bool send_warning_on_confirm;   // Отправить Предупреждение вместо ConfirmInit?
    uint8_t warning_tks;            // Тип TKS для отправки в Предупреждении
    // Можно добавить другие: потеря пакетов, неверный номер сообщения и т.д.
} SvmInstanceSettings;

// Настройки Ethernet для одного SVM
typedef struct {
    uint16_t port;
} SvmEthernetConfig;


// Основная структура конфигурации
typedef struct {
    // Общие настройки
    char interface_type[16];

    // --- Настройки для SVM ---
    SvmEthernetConfig svm_ethernet[MAX_SVM_INSTANCES];
    SvmInstanceSettings svm_settings[MAX_SVM_INSTANCES];
    bool svm_config_loaded[MAX_SVM_INSTANCES];
    int num_svm_configs_found;

    // --- Настройки для UVM ---
    EthernetConfig uvm_ethernet_target; // Параметры цели для UVM
    SerialConfig serial;                // Параметры Serial
	int uvm_keepalive_timeout_sec;

} AppConfig;

/**
 * @brief Загружает ВСЮ конфигурацию из INI-файла.
 */
int load_config(const char *filename, AppConfig *config);

#endif // CONFIG_H