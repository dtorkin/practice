/*
 * config/config.h
 *
 * Описание:
 * Определяет структуру для хранения конфигурации приложения
 * и объявляет функцию для загрузки конфигурации из файла.
 * (Версия для загрузки ВСЕХ настроек SVM)
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdbool.h>    // <-- ВКЛЮЧИТЬ для bool
#include "../io/io_interface.h" // Нужен для EthernetConfig, SerialConfig
#include "../protocol/protocol_defs.h" // Нужен для LogicalAddress

// Максимальное количество SVM, чьи настройки можно хранить и эмулировать
#define MAX_SVM_CONFIGS 4 // <-- ДОБАВЛЕНО ОПРЕДЕЛЕНИЕ

// Настройки, специфичные для одного SVM
typedef struct {
    LogicalAddress lak;
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
    SvmEthernetConfig svm_ethernet[MAX_SVM_CONFIGS];
    SvmInstanceSettings svm_settings[MAX_SVM_CONFIGS];
    bool svm_config_loaded[MAX_SVM_CONFIGS];
    int num_svm_configs_found;

    // --- Настройки для UVM ---
    EthernetConfig uvm_ethernet_target; // Параметры цели для UVM
    SerialConfig serial;                // Параметры Serial

} AppConfig;

/**
 * @brief Загружает ВСЮ конфигурацию из INI-файла.
 */
int load_config(const char *filename, AppConfig *config);

#endif // CONFIG_H