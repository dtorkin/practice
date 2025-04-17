/*
 * protocol/message_builder.h
 *
 * Описание:
 * Прототипы функций для создания различных типов сообщений протокола.
 */

#ifndef MESSAGE_BUILDER_H
#define MESSAGE_BUILDER_H

#include "protocol_defs.h" // Включаем определения структур

// --- Прототипы функций создания сообщений (От УВМ к СВ-М) ---

Message create_init_channel_message(LogicalAddress uvm_address, LogicalAddress svm_address, uint16_t message_num); // 4.2.1.
Message create_provesti_kontrol_message(LogicalAddress svm_address, uint8_t tk, uint16_t message_num); // 4.2.3.
Message create_vydat_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t vpk, uint16_t message_num); // 4.2.5.
Message create_vydat_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.7.
Message create_prinyat_parametry_so_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.9.
Message create_prinyat_time_ref_range_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.10.
Message create_prinyat_reper_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.11.
Message create_prinyat_parametry_sdr_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.12.
Message create_prinyat_parametry_3tso_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.13.
Message create_prinyat_ref_azimuth_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.14.
Message create_prinyat_parametry_tsd_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.15.
Message create_navigatsionnye_dannye_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.16.

// --- Прототипы функций создания сообщений (От СВ-М к УВМ) ---

Message create_confirm_init_message(LogicalAddress svm_address, uint8_t slp, uint8_t vdr, uint8_t bop1, uint8_t bop2, uint32_t bcb, uint16_t message_num); // 4.2.2.
Message create_podtverzhdenie_kontrolya_message(LogicalAddress svm_address, uint8_t tk, uint32_t bcb, uint16_t message_num); // 4.2.4.
Message create_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t rsk, uint16_t vsk, uint32_t bcb, uint16_t message_num); // 4.2.6.
Message create_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t kla, uint32_t sla, uint16_t ksa, uint32_t bcb, uint16_t message_num); // 4.2.8.
// ... Добавить прототипы для create_subk_message, create_ko_message и т.д. ...
Message create_preduprezhdenie_message(LogicalAddress svm_address, uint8_t tks, const uint8_t* pks, uint32_t bcb, uint16_t message_num); // 5.2

#endif // MESSAGE_BUILDER_H