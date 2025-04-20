/*
 * config/config.h
 * Описание: Определяет структуру для хранения конфигурации приложения.
 * (Модифицировано для загрузки ВСЕХ настроек SVM)
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "../io/io_interface.h"
#include "../protocol/protocol_defs.h"

// Максимальное количество SVM, чьи настройки можно хранить
#define MAX_SVM_CONFIGS 4 // Должно быть >= MAX_SVM_INSTANCES

// Настройки, специфичные для одного SVM
typedef struct {
    LogicalAddress lak;
    // Другие специфичные настройки SVM можно добавить сюда
} SvmInstanceSettings;

// Настройки Ethernet для одного SVM
typedef struct {
    uint16_t port;
    // char listen_ip[INET_ADDRSTRLEN]; // Если нужно
} SvmEthernetConfig;


// Основная структура конфигурации
typedef struct {
    // Общие настройки
    char interface_type[16];

    // --- Настройки для SVM ---
    // Массивы для хранения настроек до MAX_SVM_CONFIGS экземпляров
    SvmEthernetConfig svm_ethernet[MAX_SVM_CONFIGS];
    SvmInstanceSettings svm_settings[MAX_SVM_CONFIGS];
    // Можно добавить флаг, прочитана ли конфигурация для данного ID
    bool svm_config_loaded[MAX_SVM_CONFIGS];
    int num_svm_configs_found; // Сколько секций SVM найдено в файле

    // --- Настройки для UVM ---
    EthernetConfig uvm_ethernet_target; // Параметры цели для UVM
    SerialConfig serial;                // Параметры Serial

} AppConfig;

/**
 * @brief Загружает ВСЮ конфигурацию из INI-файла.
 * Заполняет структуру AppConfig значениями из файла.
 * Устанавливает значения по умолчанию, если файл или параметры отсутствуют.
 * @param filename Имя конфигурационного файла.
 * @param config Указатель на структуру AppConfig для заполнения.
 * @return 0 в случае успеха, -1 если ошибка файла, >0 номер строки с ошибкой парсинга.
 */
int load_config(const char *filename, AppConfig *config); // <-- Сигнатура с 2 аргументами

#endif // CONFIG_H