/*
 * uvm/uvm_comm.h
 *
 * Описание:
 * Прототипы функций для взаимодействия UVM с SVM, инкапсулирующие
 * отправку запросов и получение ответов через IOInterface.
 */

#ifndef UVM_COMM_H
#define UVM_COMM_H

#include "../protocol/protocol_defs.h"
#include "../io/io_interface.h" // Включаем для IOInterface

/**
 * @brief Отправляет сообщение "Инициализация канала" и ожидает "Подтверждение инициализации".
 *
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений (будет инкрементирован).
 * @param receivedMessage Указатель на структуру Message для сохранения полученного ответа.
 * @return Указатель на тело полученного сообщения ConfirmInitBody в случае успеха, NULL в случае ошибки или неверного типа ответа.
 *         Память для receivedMessage должна быть выделена вызывающей стороной.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
ConfirmInitBody* send_init_channel_and_receive_confirm(IOInterface *io, uint16_t *messageCounter, Message *receivedMessage);

/**
 * @brief Отправляет сообщение "Провести контроль" и ожидает "Подтверждение контроля".
 *
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @param receivedMessage Указатель на структуру для ответа.
 * @param tk Тип контроля для отправки.
 * @return Указатель на тело PodtverzhdenieKontrolyaBody в случае успеха, NULL иначе.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
PodtverzhdenieKontrolyaBody* send_provesti_kontrol_and_receive_podtverzhdenie(IOInterface *io, uint16_t *messageCounter, Message *receivedMessage, uint8_t tk);

/**
 * @brief Отправляет сообщение "Выдать результаты контроля" и ожидает "Результаты контроля".
 *
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @param receivedMessage Указатель на структуру для ответа.
 * @param vpk Вид запроса результатов контроля.
 * @return Указатель на тело RezultatyKontrolyaBody в случае успеха, NULL иначе.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
RezultatyKontrolyaBody* send_vydat_rezultaty_kontrolya_and_receive_rezultaty(IOInterface *io, uint16_t *messageCounter, Message *receivedMessage, uint8_t vpk);

/**
 * @brief Отправляет сообщение "Выдать состояние линии" и ожидает "Состояние линии".
 *
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @param receivedMessage Указатель на структуру для ответа.
 * @return Указатель на тело SostoyanieLiniiBody в случае успеха, NULL иначе.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
SostoyanieLiniiBody* send_vydat_sostoyanie_linii_and_receive_sostoyanie(IOInterface *io, uint16_t *messageCounter, Message *receivedMessage);


// --- Функции только для отправки ---

/**
 * @brief Отправляет сообщение "Принять параметры СО".
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
int send_prinyat_parametry_so(IOInterface *io, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять TIME_REF_RANGE".
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
int send_prinyat_time_ref_range(IOInterface *io, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять Reper".
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
int send_prinyat_reper(IOInterface *io, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять параметры СДР".
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
int send_prinyat_parametry_sdr(IOInterface *io, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять параметры 3ЦО".
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
int send_prinyat_parametry_3tso(IOInterface *io, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять REF_AZIMUTH".
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
int send_prinyat_ref_azimuth(IOInterface *io, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять параметры ЦДР".
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
int send_prinyat_parametry_tsd(IOInterface *io, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Навигационные данные".
 * @param io Указатель на инициализированный IOInterface.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
 // ИСПРАВЛЕНО: int clientSocketFD -> IOInterface *io
int send_navigatsionnye_dannye(IOInterface *io, uint16_t *messageCounter);


#endif // UVM_COMM_H