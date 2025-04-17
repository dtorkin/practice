/*
 * uvm/uvm_comm.h
 *
 * Описание:
 * Прототипы функций для взаимодействия UVM с SVM, инкапсулирующие
 * отправку запросов и получение ответов.
 */

#ifndef UVM_COMM_H
#define UVM_COMM_H

#include "../protocol/protocol_defs.h" // Используем относительный путь

/**
 * @brief Отправляет сообщение "Инициализация канала" и ожидает "Подтверждение инициализации".
 *
 * @param clientSocketFD Дескриптор сокета для связи с SVM.
 * @param messageCounter Указатель на счетчик исходящих сообщений (будет инкрементирован).
 * @param receivedMessage Указатель на структуру Message для сохранения полученного ответа.
 * @return Указатель на тело полученного сообщения ConfirmInitBody в случае успеха, NULL в случае ошибки или неверного типа ответа.
 *         Память для receivedMessage должна быть выделена вызывающей стороной.
 */
ConfirmInitBody* send_init_channel_and_receive_confirm(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage);

/**
 * @brief Отправляет сообщение "Провести контроль" и ожидает "Подтверждение контроля".
 *
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @param receivedMessage Указатель на структуру для ответа.
 * @param tk Тип контроля для отправки.
 * @return Указатель на тело PodtverzhdenieKontrolyaBody в случае успеха, NULL иначе.
 */
PodtverzhdenieKontrolyaBody* send_provesti_kontrol_and_receive_podtverzhdenie(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage, uint8_t tk);

/**
 * @brief Отправляет сообщение "Выдать результаты контроля" и ожидает "Результаты контроля".
 *
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @param receivedMessage Указатель на структуру для ответа.
 * @param vpk Вид запроса результатов контроля.
 * @return Указатель на тело RezultatyKontrolyaBody в случае успеха, NULL иначе.
 */
RezultatyKontrolyaBody* send_vydat_rezultaty_kontrolya_and_receive_rezultaty(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage, uint8_t vpk);

/**
 * @brief Отправляет сообщение "Выдать состояние линии" и ожидает "Состояние линии".
 *
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @param receivedMessage Указатель на структуру для ответа.
 * @return Указатель на тело SostoyanieLiniiBody в случае успеха, NULL иначе.
 */
SostoyanieLiniiBody* send_vydat_sostoyanie_linii_and_receive_sostoyanie(int clientSocketFD, uint16_t *messageCounter, Message *receivedMessage);


// --- Функции только для отправки ---

/**
 * @brief Отправляет сообщение "Принять параметры СО".
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int send_prinyat_parametry_so(int clientSocketFD, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять TIME_REF_RANGE".
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int send_prinyat_time_ref_range(int clientSocketFD, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять Reper".
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int send_prinyat_reper(int clientSocketFD, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять параметры СДР".
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int send_prinyat_parametry_sdr(int clientSocketFD, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять параметры 3ЦО".
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int send_prinyat_parametry_3tso(int clientSocketFD, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять REF_AZIMUTH".
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int send_prinyat_ref_azimuth(int clientSocketFD, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Принять параметры ЦДР".
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int send_prinyat_parametry_tsd(int clientSocketFD, uint16_t *messageCounter);

/**
 * @brief Отправляет сообщение "Навигационные данные".
 * @param clientSocketFD Дескриптор сокета.
 * @param messageCounter Указатель на счетчик исходящих сообщений.
 * @return 0 в случае успеха, -1 в случае ошибки.
 */
int send_navigatsionnye_dannye(int clientSocketFD, uint16_t *messageCounter);


#endif // UVM_COMM_H