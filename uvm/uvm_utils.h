/*
 * uvm/uvm_utils.h
 *
 * Описание:
 * Прототипы вспомогательных функций для модуля UVM.
 */
#ifndef UVM_UTILS_H
#define UVM_UTILS_H

#include "uvm_types.h" // Для UvmRequestType

/**
 * @brief Преобразует тип запроса UVM в строковое представление имени сообщения.
 * @param req_type Тип запроса UVM.
 * @return Константная строка с именем или "Неизвестный запрос UVM".
 */
const char* uvm_request_type_to_message_name(UvmRequestType req_type);

/**
 * @brief Ожидает конкретный ответ от указанного SVM.
 *
 * @param target_svm_id ID SVM, от которого ожидается ответ.
 * @param expected_msg_type Ожидаемый тип сообщения.
 * @param response_message Указатель, куда будет скопировано полученное сообщение.
 * @param timeout_ms Таймаут ожидания в миллисекундах.
 * @return true, если ожидаемый ответ получен, false при таймауте или ошибке.
 */
bool wait_for_specific_response(
    int target_svm_id,
    MessageType expected_msg_type,
    UvmResponseMessage *response_message, // Принимает указатель для записи ответа
    int timeout_ms
);

#endif // UVM_UTILS_H