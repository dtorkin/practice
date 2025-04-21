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

#endif // UVM_UTILS_H