// common.h

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

// Константы
#define MAX_MESSAGE_BODY_SIZE 65522 // Максимальный размер тела сообщения (из раздела 4.1)
#define LOGICAL_ADDRESS_UVM              0x01 // Логический адрес УВМ (из таблицы 4.2)
#define LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1 0x08 // Пример логического адреса СВ-М (из таблицы 4.2)

// Перечисление: Логические адреса (из таблицы 4.2)
typedef enum {
    LOGICAL_ADDRESS_UVM_VAL = LOGICAL_ADDRESS_UVM,
    LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL = LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1,
} LogicalAddress;

// Перечисление: Типы сообщений (из таблицы 4.4)
typedef enum {
    MESSAGE_TYPE_INIT_CHANNEL             = 128, // «Инициализация канала»
    MESSAGE_TYPE_CONFIRM_INIT             = 129, // «Подтверждение инициализации канала»
    MESSAGE_TYPE_PROVESTI_KONTROL         = 130, // «Провести контроль»
    MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA = 131, // «Подтверждение контроля»
    MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA = 132, // «Выдать результаты контроля»
    MESSAGE_TYPE_RESULTATY_KONTROLYA      = 133, // «Результаты контроля»
    MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII    = 134, // «Выдать состояние линии»
    MESSAGE_TYPE_SOSTOYANIE_LINII          = 135, // «Состояние линии»
    MESSAGE_TYPE_SOSTOYANIE_LINII_136      = 136, // «Состояние линии» - Заглушка для 4.2.6
    MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII_137 = 137, // «Выдать состояние линии» - Заглушка для 4.2.7
    MESSAGE_TYPE_SOSTOYANIE_LINII_138      = 138, // «Состояние линии» - Заглушка для 4.2.8
} MessageType;

// Структура: Флаги (из таблицы 4.3)
typedef struct {
    uint8_t np : 1;       // Направление передачи (0 - от УВМ к СВ-М, 1 - от СВ-М к УВМ)
    uint8_t hc_t_bp : 1;  // Восьмой разряд номера сообщения
    uint8_t hc_ct_bp : 1; // Девятый разряд номера сообщения
    uint8_t hc_ct10p : 1; // Десятый разряд номера сообщения
    uint8_t reserve4 : 1; // Резерв
    uint8_t reserve5 : 1; // Резерв
    uint8_t reserve6 : 1; // Резерв
    uint8_t reserve7 : 1; // Резерв
} MessageFlags;

// Структура: Заголовок сообщения (из таблицы 4.1)
typedef struct {
    uint8_t address;         // Адрес (1 байт)
    MessageFlags flags;      // Флаги (1 байт)
    uint16_t body_length;    // Длина тела сообщения (2 байта)
    uint8_t message_number;  // Номер сообщения (1 байт, младшие 8 бит номера)
    uint8_t message_type;    // Тип сообщения (1 байт)
} MessageHeader;

// Структуры: Тела сообщений (из разделов 4.2.1 - 4.2.8)
typedef struct {
    uint8_t lauvm; // Логический адрес УВМ
    uint8_t lak;   // Логический адрес СВ-М
} InitChannelBody;

typedef struct {
    uint8_t lak;   // Логический адрес СВ-М
    uint8_t slp;   // Состояние линий передач СВ-М (СЛП)
    uint8_t vdr;   // Версия прошивки ПЛИС модуля МОДР (ВДР)
    uint8_t vor1;  // Версия прошивки ПЛИС модуля МОСВ1 (BOP1)
    uint8_t vor2;  // Версия прошивки ПЛИС модуля МОСВ2 (BOP2)
    uint32_t bcb;  // Состояние BCB
} ConfirmInitBody;

typedef struct {
    uint8_t tk; // Тип контроля (ТК)
} ProvestiKontrolBody;

typedef struct {
    uint8_t lak;  // Логический адрес СВ-М (ЛАК)
    uint8_t tk;   // Тип контроля (ТК) - из запроса
    uint32_t bcb; // Состояние ВСВ (BCB)
} PodtverzhdenieKontrolyaBody;

typedef struct {
    uint8_t vpk; // Вид запроса выдачи результатов контроля (ВРК)
} VydatRezultatyKontrolyaBody;

typedef struct {
    uint8_t lak;   // Логический адрес СВ-М (ЛАК)
    uint8_t rsk;     // Результаты контроля СВ-М (РСК)
    uint16_t bck;    // Время прохождения самоконтроля (ВСК)
    uint32_t bcb;    // Состояние ВСВ (BCB)
} RezultatyKontrolyaBody;

typedef struct {
    // Тело сообщения "Выдать состояние линии" - пустое
} VydatSostoyanieLiniiBody;

typedef struct {
    uint8_t lak;    // Логический адрес СВ-М (ЛАК)
    uint16_t kla;   // Количество изменений состояния LinkUp (КЛА)
    uint32_t sla;   // Состояние счётчика интегрального времени LinkUp (СЛА)
    uint16_t ksa;   // Количество изменений состояния SignDet (КСА)
    uint32_t bcb;   // Состояние ВСВ (BCB)
} SostoyanieLiniiBody;

typedef struct {
    uint8_t lak;          // Логический адрес СВ-М (ЛАК) - Заглушка для 4.2.6
    uint32_t bcb;         // Состояние ВСВ (BCB) - Заглушка для 4.2.6
} SostoyanieLinii136Body;

typedef struct {
    // Пустое тело, как в VydatSostoyanieLiniiBody - Заглушка для 4.2.7
} VydatSostoyanieLinii137Body;

typedef struct {
    uint8_t lak;          // Логический адрес СВ-М (ЛАК) - Заглушка для 4.2.8
    uint32_t bcb;         // Состояние ВСВ (BCB) - Заглушка для 4.2.8
} SostoyanieLinii138Body;

// Структура: Сообщение (Общая структура сообщения) (из таблицы 4.1)
typedef struct {
    MessageHeader header;
    uint8_t body[MAX_MESSAGE_BODY_SIZE]; // Тело сообщения (максимальный размер)
} Message;

// Прототипы функций: Создание сообщений
Message create_init_channel_message(LogicalAddress uvm_address, LogicalAddress svm_address, uint16_t message_num);
Message create_confirm_init_message(LogicalAddress svm_address, uint8_t slp, uint8_t vdr, uint8_t vor1, uint8_t vor2, uint32_t bcb, uint16_t message_num);
Message create_provesti_kontrol_message(LogicalAddress svm_address, uint8_t tk, uint16_t message_num);
Message create_podtverzhdenie_kontrolya_message(LogicalAddress svm_address, uint8_t tk, uint32_t bcb, uint16_t message_num);
Message create_vydat_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t vpk, uint16_t message_num);
Message create_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t rsk, uint16_t bck, uint32_t bcb, uint16_t message_num);
Message create_vydat_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t message_num);
Message create_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t kla, uint32_t sla, uint16_t ksa, uint32_t bcb, uint16_t message_num);
Message create_sostoyanie_linii_136_message(LogicalAddress svm_address, uint32_t bcb, uint16_t message_num);
Message create_vydat_sostoyanie_linii_137_message(LogicalAddress svm_address, uint16_t message_num);
Message create_sostoyanie_linii_138_message(LogicalAddress svm_address, uint32_t bcb, uint16_t message_num);

// Прототипы функций: Преобразование порядка байтов
uint16_t get_full_message_number(const MessageHeader *header);
void message_to_network_byte_order(Message *message);
void message_to_host_byte_order(Message *message);

// Прототип функции: Отправка сообщений
int send_message(int socketFD, Message *message);

// Перечисление: Состояние SVM (SVM State)
typedef enum {
    STATE_NOT_INITIALIZED, // Не инициализировано
    STATE_INITIALIZED,     // Инициализировано
    STATE_SELF_TEST         // Самоконтроль
} SVMState;

#endif // COMMON_H