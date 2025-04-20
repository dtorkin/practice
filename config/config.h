/*
 * config/config.h
 *
 * Описание:
 * Определяет структуру для хранения конфигурации приложения
 * и объявляет функцию для загрузки конфигурации из файла.
 */

#ifndef CONFIG_H
#define CONFIG_H

// Включаем определения интерфейсов, где теперь определены EthernetConfig и SerialConfig
#include "../io/io_interface.h" // Путь может быть ../io/io_interface.h в зависимости от структуры include

// Основная структура конфигурации
typedef struct {
    // Секция [communication]
    char interface_type[16]; // "ethernet" или "serial"

    // Секция [ethernet]
    EthernetConfig ethernet; // Тип теперь известен из io_interface.h

    // Секция [serial]
    SerialConfig serial;     // Тип теперь известен из io_interface.h

    int num_svm_instances; // Количество эмулируемых SVM
    int base_svm_lak;      // Базовый логический адрес для первого 
    // Можно добавить другие секции и параметры по мере необходимости

} AppConfig;

/**
 * @brief Загружает конфигурацию из INI-файла.
 *
 * Заполняет структуру AppConfig значениями из файла.
 * Устанавливает значения по умолчанию, если файл или параметры отсутствуют.
 *
 * @param filename Имя конфигурационного файла.
 * @param config Указатель на структуру AppConfig для заполнения.
 * @return 0 в случае успеха, -1 если файл не найден, >0 если ошибка парсинга (код ошибки inih).
 */
int load_config(const char *filename, AppConfig *config);

#endif // CONFIG_H