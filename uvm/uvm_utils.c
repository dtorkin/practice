/*
 * uvm/uvm_utils.c
 *
 * Описание:
 * Вспомогательные функции для модуля UVM.
 */
#include "uvm_types.h" // Для UvmRequestType
#include <stdio.h>     // Для NULL

const char* uvm_request_type_to_message_name(UvmRequestType req_type) {
    switch (req_type) {
        case UVM_REQ_INIT_CHANNEL:      return "Инициализация канала"; // Соответствует MESSAGE_TYPE_INIT_CHANNEL
        case UVM_REQ_PROVESTI_KONTROL:    return "Провести контроль";    // Соответствует MESSAGE_TYPE_PROVESTI_KONTROL
        case UVM_REQ_VYDAT_REZULTATY:   return "Выдать результаты контроля"; // Соответствует MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA
        case UVM_REQ_VYDAT_SOSTOYANIE:  return "Выдать состояние линии"; // Соответствует MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII
        case UVM_REQ_PRIYAT_PARAM_SO:   return "Принять параметры СО";   // Соответствует MESSAGE_TYPE_PRIYAT_PARAMETRY_SO
        case UVM_REQ_PRIYAT_TIME_REF:   return "Принять TIME_REF_RANGE"; // Соответствует MESSAGE_TYPE_PRIYAT_TIME_REF_RANGE
        case UVM_REQ_PRIYAT_REPER:      return "Принять Reper";          // Соответствует MESSAGE_TYPE_PRIYAT_REPER
        case UVM_REQ_PRIYAT_PARAM_SDR:  return "Принять параметры СДР";  // Соответствует MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR
        case UVM_REQ_PRIYAT_PARAM_3TSO: return "Принять параметры 3ЦО";  // Соответствует MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO
        case UVM_REQ_PRIYAT_REF_AZIMUTH:return "Принять REF_AZIMUTH";    // Соответствует MESSAGE_TYPE_PRIYAT_REF_AZIMUTH
        case UVM_REQ_PRIYAT_PARAM_TSD:  return "Принять параметры ЦДР";  // Соответствует MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD
        case UVM_REQ_PRIYAT_NAV_DANNYE: return "Навигационные данные";   // Соответствует MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE
        case UVM_REQ_SHUTDOWN:          return "Запрос Shutdown";
        case UVM_REQ_NONE:
        default:                        return "Неизвестный запрос UVM";
    }
}