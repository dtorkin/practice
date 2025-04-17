/*
 * io/io_ethernet.h
 *
 * Описание:
 * Объявление фабричной функции для создания Ethernet IO интерфейса.
 */

#ifndef IO_ETHERNET_H
#define IO_ETHERNET_H

#include "io_interface.h" // Включаем определение базового интерфейса
// Не включаем config.h напрямую, фабрика получит его снаружи

/**
 * @brief Создает и инициализирует экземпляр IOInterface для Ethernet.
 * @param config Указатель на структуру с параметрами Ethernet.
 *               Функция СОЗДАСТ КОПИЮ этой структуры, память для config может быть освобождена после вызова.
 * @return Указатель на созданный IOInterface или NULL в случае ошибки.
 *         Память для возвращенной структуры должна быть освобождена с помощью io->destroy(io).
 */
IOInterface* create_ethernet_interface(const EthernetConfig *config);

#endif // IO_ETHERNET_H