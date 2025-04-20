/*
 * config/config.h
 * Описание: Определяет структуру для хранения конфигурации приложения.
 * (Модифицировано для поддержки секций для разных SVM)
 */
#ifndef CONFIG_H
#define CONFIG_H

#include "../io/io_interface.h"
#include "../protocol/protocol_defs.h" // Для LogicalAddress

// Настройки, специфичные для одного SVM
typedef struct {
    LogicalAddress lak; // Логический адрес этого экземпляра SVM
    // Другие специфичные настройки SVM можно добавить сюда
} SvmInstanceSettings;

// Основная структура конфигурации
typedef struct {
    // Общие настройки
    char interface_type[16]; // "ethernet" или "serial" (сейчас только ethernet)

    // Настройки для конкретного экземпляра SVM (читаются на основе ID)
    EthernetConfig ethernet; // Параметры Ethernet (порт)
    SvmInstanceSettings svm_settings; // Параметры SVM (LAK)

    // Параметры для UVM (оставлены для совместимости парсера)
    EthernetConfig uvm_ethernet_target; // Параметры цели для UVM
    SerialConfig serial; // Параметры Serial (для UVM или старого SVM)

} AppConfig;

/**
 * @brief Загружает конфигурацию из INI-файла для указанного ID SVM.
 * @param filename Имя файла.
 * @param config Указатель на структуру для заполнения.
 * @param svm_id ID экземпляра SVM (0, 1, 2...), для которого читается конфигурация.
 * @return 0 в случае успеха, -1 если ошибка файла, >0 номер строки с ошибкой парсинга.
 */
int load_config(const char *filename, AppConfig *config, int svm_id);

#endif // CONFIG_H