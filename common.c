// common.c

#include "common.h"
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>

/********************************************************************************/
/*                ФУНКЦИИ СОЗДАНИЯ СООБЩЕНИЙ (create_***_message)                */
/********************************************************************************/

// [4.2.1] «Инициализация канала» (создание сообщения)
Message create_init_channel_message(LogicalAddress uvm_address, LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 0;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(InitChannelBody));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_INIT_CHANNEL;

    InitChannelBody *body = (InitChannelBody *) message.body;
    body->lauvm = uvm_address;
    body->lak = svm_address;

    return message;
}

// [4.2.2] «Подтверждение инициализации канала» (создание сообщения)
Message create_confirm_init_message(LogicalAddress svm_address, uint8_t slp, uint8_t vdr, uint8_t vor1, uint8_t vor2,
                                    uint32_t bcb, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 1;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(ConfirmInitBody));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_CONFIRM_INIT;

    ConfirmInitBody *body = (ConfirmInitBody *) message.body;
    body->lak = svm_address;
    body->slp = slp;
    body->vdr = vdr;
    body->vor1 = vor1;
    body->vor2 = vor2;
    body->bcb = htonl(bcb);

    return message;
}

// [4.2.3] «Провести контроль» (создание сообщения)
Message create_provesti_kontrol_message(LogicalAddress svm_address, uint8_t tk, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 0;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(ProvestiKontrolBody));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_PROVESTI_KONTROL;

    ProvestiKontrolBody *body = (ProvestiKontrolBody *) message.body;
    body->tk = tk;

    return message;
}

// [4.2.4] «Подтверждение контроля» (создание сообщения)
Message
create_podtverzhdenie_kontrolya_message(LogicalAddress svm_address, uint8_t tk, uint32_t bcb, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 1;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(PodtverzhdenieKontrolyaBody));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA;

    PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody *) message.body;
    body->lak = svm_address;
    body->tk = tk;
    body->bcb = htonl(bcb);

    return message;
}

// [4.2.5] «Выдать результаты контроля» (создание сообщения)
Message create_vydat_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t vpk, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 0;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(VydatRezultatyKontrolyaBody));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA;

    VydatRezultatyKontrolyaBody *body = (VydatRezultatyKontrolyaBody *) message.body;
    body->vpk = vpk;

    return message;
}

// [4.2.6] «Результаты контроля» (создание сообщения)
Message create_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t rsk, uint16_t bck, uint32_t bcb,
                                           uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 1;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(RezultatyKontrolyaBody));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_RESULTATY_KONTROLYA;

    RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody *) message.body;
    body->lak = svm_address;
    body->rsk = rsk;
    body->bck = htons(bck);
    body->bcb = htonl(bcb);

    return message;
}

// [4.2.7] «Выдать состояние линии» (создание сообщения)
Message create_vydat_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 0;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(0); // Пустое тело
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII;

    return message;
}

// [4.2.8] «Состояние линии» (создание сообщения)
Message
create_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t kla, uint32_t sla, uint16_t ksa, uint32_t bcb,
                                uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 1;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(SostoyanieLiniiBody));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_SOSTOYANIE_LINII;

    SostoyanieLiniiBody *body = (SostoyanieLiniiBody *) message.body;
    body->lak = svm_address;
    body->kla = htons(kla);
    body->sla = htonl(sla);
    body->ksa = htons(ksa);
    body->bcb = htonl(bcb);

    return message;
}

// [4.2.9] «Принять параметры СО» (создание сообщения)
Message create_prinyat_parametry_so_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message)); 

    // --- Настройка заголовка ---
    message.header.address = svm_address;
    message.header.flags.np = 0; // UVM -> SVM
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    // Убедимся в правильной длине тела на основе *нового* размера структуры (54 байта)
    message.header.body_length = htons(sizeof(PrinyatParametrySoBody));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_PRIYAT_PARAMETRY_SO; // 160

    // --- Настройка тела ---
    PrinyatParametrySoBody *body = (PrinyatParametrySoBody *) message.body;

    // Инициализируем поля согласно порядку байтов в Таблице 4.22, используя примеры значений
    body->pp = MODE_VR;       // Пример: Режим VR
    body->brl = 0x07;         // Пример: Маска бланкирования (лучи 1,2,3 не бланкированы)
    body->q0 = 0x03;          // Пример: Порог помехи
    body->q = htons(1500);     // Пример: Нормализованная константа шума (нужен htons)
    body->knk = htons(300);      // Пример: KNK (нужен htons)
    body->knk_or1 = htons(350);  // Пример: KNK_OR1 (нужен htons)
    // Пример: Заполняем массив Weight
    for (int i = 0; i < 23; ++i) {
        body->weight[i] = 10 + i; // Пример знаковых/беззнаковых 8-битных значений
    }
    body->l1 = htons(100);       // Пример: Длина опоры L1 (нужен htons)
    body->l2 = htons(150);       // Пример: Длина опоры L2 (нужен htons)
    body->l3 = htons(200);       // Пример: Длина опоры L3 (нужен htons)
    body->aru = 0x01;         // Пример: Режим АРУ (1 - измерительный)
    body->karu = 0x0A;        // Пример: KARU
    body->sigmaybm = htons(2500); // Пример: SIGMAYBM (нужен htons)
    body->rgd = htons(1024);    // Пример: Длина строки РГД (нужен htons) - Поле отсутствовало!
    body->yo = 0x01;          // Пример: Уровень обработки (1 - с обработкой)
    body->a2 = 0x05;          // Пример: A2 (Коэффициент) - Раньше было 'az'
    body->fixp = htons(100);    // Пример: Уровень фиксированного порога (нужен htons)
    return message;
}

// [4.2.12] «Принять параметры СДР» (создание сообщения)
Message create_prinyat_parametry_sdr_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 0;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(PrinyatParametrySdrBody));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR; // Используем добавленный тип

    PrinyatParametrySdrBody *body = (PrinyatParametrySdrBody *) message.body;
    body->pp_nl = 0x01;        // Пример режима работы РСА и номера луча
    body->brl = 0x07;         // Пример маски бланкирования
    body->kdec = 0x02;        // Пример коэффициента прореживания
    body->yo = 0x01;          // Пример уровня обработки
    body->sland = 0x10;       // Пример доли суши
    body->sf = 0x05;          // Пример доли НК
    body->t0 = 0x20;          // Пример порога суши
    body->t1 = 0x15;          // Пример порога НК на море
    body->q0 = 0x03;          // Пример порога помехи
    body->q = htons(1500);     // Пример нормализованной константы шума
    body->aru = 0x01;         // Пример режима работы АРУ
    body->karu = 0x0A;        // Пример константы кода аттенюации устр-ва МПУ 11B521-4 в режиме внешнего кода АРУ (KARU)
    body->sigmaybm = htons(
            2500); // Пример константы номинального среднеквадратичного уровня шума на выходе устройства МПУ 11B521-4 (SIGMAYBM)
    body->kw = 0x01;          // Пример кода включения взвешивания
    for (int i = 0; i < 23; ++i) { // Заполнение массива W[23] примерами данных
        body->w_23[i] = 0x0F + i;
    }
    body->nfft = htons(128);    // Пример количества отсчётов БПФ
    body->or_param = 0x08;      // Пример размера ячейки OR
    body->oa = 0x0A;          // Пример размера ячейки OA
    body->mrr = htons(500);     // Пример количества отсчетов в опоре
    body->fixp = htons(100);    // Пример уровня фиксированного порога


    return message;
}

// [4.2.13] «Принять параметры 3ЦО» (создание сообщения)
Message create_prinyat_parametry_3tso_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    // --- Настройка заголовка ---
    message.header.address = svm_address;
    message.header.flags.np = 0; // UVM -> SVM
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(PrinyatParametry3TsoBody)); // 314 байт
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO; // 200

    // --- Настройка тела (примеры значений) ---
    PrinyatParametry3TsoBody *body = (PrinyatParametry3TsoBody *) message.body;

    body->Rezerv = htons(0);        // Пример: Резерв
    body->Ncadr = htons(1024);      // Пример: Количество строк дальности
    body->Xnum = 128;               // Пример: Количество строк для OP1
    // Пример: Заполнение массивов DNA (можно использовать реальные данные)
    for (int i = 0; i < 8; ++i) {
        for (int j = 0; j < 16; ++j) {
            body->DNA[i][j] = (uint8_t)(i * 16 + j);
            body->DNA_INVERS[i][j] = (uint8_t)(255 - (i * 16 + j));
        }
    }
    for (int i = 0; i < 16; ++i) {
        body->DNA_OR1[i] = (uint8_t)(i + 10);
        body->DNA_INVERS_OR1[i] = (uint8_t)(255 - (i + 10));
        body->Sea_or_land[i] = (i % 2 == 0) ? 0xFF : 0x00; // Пример береговой линии
    }
    body->Q1 = htons(500);          // Пример: Q1
    body->Q1_OR1 = htons(600);      // Пример: Q1_OR1
    body->Part = 0x80;              // Пример: Доля суши (50%)

    return message;
}

// [4.2.15] «Принять параметры ЦДР» (создание сообщения)
Message create_prinyat_parametry_tsd_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 0;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(PrinyatParametryTsdBody)); // Используем структуру PrinyatParametryTsdBody
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD; // Используем добавленный тип

    PrinyatParametryTsdBody *body = (PrinyatParametryTsdBody *) message.body;
    body->rezerv = 0;                      // Пример резерва
    body->nin = htons(256);                // Пример Nin
    body->nout = htons(128);               // Пример Nout
    body->mrn = htons(64);                 // Пример Mrn
    body->shmr = 10;                       // Пример ShMR
    body->nar = 32;                        // Пример NAR
    // Заполнение массивов примерами (в реальном коде нужно заполнить данными)
    for (int i = 0; i < 1024; ++i) body->okm[i] = i % 128 - 64; // Пример для okm
    for (int i = 0; i < 1024; ++i) body->hshmr[i] = i % 256;   // Пример для hshmr
    for (int i = 0; i < 54400; ++i) body->har[i] = i % 256;     // Пример для har (заполнитель uint8_t)


    return message;
}

// [4.2.16] «Навигационные данные» (создание сообщения)
Message create_navigatsionnye_dannye_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 0;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(
            sizeof(NavigatsionnyeDannyeBody)); // Используем структуру NavigatsionnyeDannyeBody
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE; // Используем добавленный тип

    NavigatsionnyeDannyeBody *body = (NavigatsionnyeDannyeBody *) message.body;
    // Заполнение массива mnd[] примерами навигационных данных (в реальности - данные)
    for (int i = 0; i < 256; ++i) {
        body->mnd[i] = i; // Простые значения для примера
    }

    return message;
}

/********************************************************************************/
/*                  ФУНКЦИИ ПРЕОБРАЗОВАНИЯ И ОТПРАВКИ СООБЩЕНИЙ                  */
/********************************************************************************/

// Получить полный номер сообщения (на основе флагов и номера сообщения)
uint16_t get_full_message_number(const MessageHeader *header) {
    uint16_t highBits = (header->flags.hc_t_bp << 8) | (header->flags.hc_ct_bp << 9) | (header->flags.hc_ct10p << 10);
    return (highBits | header->message_number);
}

// Преобразовать сообщение в сетевой порядок байтов (Network Byte Order)
void message_to_network_byte_order(Message *message) {
  message->header.body_length = htons(ntohs(message->header.body_length)); // Сначала в host, потом обратно в network
                                                                           // Это нужно, чтобы корректно работать с уже преобразованными данными, если функция вызывается повторно
  if (message->header.message_type == MESSAGE_TYPE_RESULTATY_KONTROLYA) {
    RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody *)message->body;
    body->bck = htons(ntohs(body->bck)); // host -> network
  } else if (message->header.message_type == MESSAGE_TYPE_SOSTOYANIE_LINII) {
    SostoyanieLiniiBody *body = (SostoyanieLiniiBody *)message->body;
    body->kla = htons(ntohs(body->kla)); // host -> network
    body->ksa = htons(ntohs(body->ksa)); // host -> network
    // bcb и sla (uint32_t) обрабатываются отдельно в send/recv через htonl/ntohl
  } else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR) {
    PrinyatParametrySdrBody *body = (PrinyatParametrySdrBody *)message->body;
    body->q = htons(ntohs(body->q));
    body->sigmaybm = htons(ntohs(body->sigmaybm));
    body->nfft = htons(ntohs(body->nfft));
    body->mrr = htons(ntohs(body->mrr));
    body->fixp = htons(ntohs(body->fixp));
  } else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD) {
    PrinyatParametryTsdBody *body = (PrinyatParametryTsdBody *)message->body;
    body->nin = htons(ntohs(body->nin));
    body->nout = htons(ntohs(body->nout));
    body->mrn = htons(ntohs(body->mrn));
    body->rezerv = htons(ntohs(body->rezerv));
  } else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_SO) {
    PrinyatParametrySoBody *body = (PrinyatParametrySoBody *)message->body;
    body->q = htons(ntohs(body->q));
    body->knk = htons(ntohs(body->knk));
    body->knk_or1 = htons(ntohs(body->knk_or1));
    body->l1 = htons(ntohs(body->l1));
    body->l2 = htons(ntohs(body->l2));
    body->l3 = htons(ntohs(body->l3));
    body->sigmaybm = htons(ntohs(body->sigmaybm));
    body->rgd = htons(ntohs(body->rgd));
    body->fixp = htons(ntohs(body->fixp));
  } else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO) {
    PrinyatParametry3TsoBody *body = (PrinyatParametry3TsoBody *)message->body;
    body->Rezerv = htons(ntohs(body->Rezerv));
    body->Ncadr = htons(ntohs(body->Ncadr));
    body->Q1 = htons(ntohs(body->Q1));
    body->Q1_OR1 = htons(ntohs(body->Q1_OR1));
  }
  // Для uint32_t полей (bcb, sla) преобразование делается при отправке/получении с использованием htonl/ntohl
}

// Преобразовать сообщение в порядок байтов хоста (Host Byte Order)
void message_to_host_byte_order(Message *message) {
    message->header.body_length = ntohs(message->header.body_length);
    if (message->header.message_type == MESSAGE_TYPE_CONFIRM_INIT) {
        ConfirmInitBody *body = (ConfirmInitBody *) message->body;
        body->bcb = ntohl(body->bcb);
    } else if (message->header.message_type == MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA) {
        PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody *) message->body;
        body->bcb = ntohl(body->bcb);
    } else if (message->header.message_type == MESSAGE_TYPE_RESULTATY_KONTROLYA) {
        RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody *) message->body;
        body->bck = ntohs(body->bck);
        body->bcb = ntohl(body->bcb);
    } else if (message->header.message_type == MESSAGE_TYPE_SOSTOYANIE_LINII) {
        SostoyanieLiniiBody *body = (SostoyanieLiniiBody *) message->body;
        body->kla = ntohs(body->kla);
        body->sla = ntohl(body->sla);
        body->ksa = ntohs(body->ksa);
        body->bcb = ntohl(body->bcb);
    } else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR) {
        PrinyatParametrySdrBody *body = (PrinyatParametrySdrBody *) message->body;
        body->q = ntohs(body->q);
        body->sigmaybm = ntohs(body->sigmaybm);
        body->nfft = ntohs(body->nfft);
        body->mrr = ntohs(body->mrr);
        body->fixp = ntohs(body->fixp);
    } else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD) {
        PrinyatParametryTsdBody *body = (PrinyatParametryTsdBody *) message->body;
        body->nin = ntohs(body->nin);
        body->nout = ntohs(body->nout);
        body->mrn = ntohs(body->mrn);
        body->rezerv = ntohs(body->rezerv);
    } else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_SO) {
        PrinyatParametrySoBody *body = (PrinyatParametrySoBody *) message->body;
        body->q = ntohs(body->q);
        body->knk = ntohs(body->knk);
        body->knk_or1 = ntohs(body->knk_or1);
        body->l1 = ntohs(body->l1);
        body->l2 = ntohs(body->l2);
        body->l3 = ntohs(body->l3);
        body->sigmaybm = ntohs(body->sigmaybm);
        body->rgd = ntohs(body->rgd);
        body->fixp = ntohs(body->fixp);
    } else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO) {
        PrinyatParametry3TsoBody *body = (PrinyatParametry3TsoBody *) message->body;
        body->Rezerv = ntohs(body->Rezerv);
        body->Ncadr = ntohs(body->Ncadr);
        body->Q1 = ntohs(body->Q1);
        body->Q1_OR1 = ntohs(body->Q1_OR1);
    }
}

// Отправить сообщение через сокет
int send_message(int socketFD, Message *message) {
    message_to_network_byte_order(message);
    ssize_t bytes_sent = send(socketFD, message, sizeof(MessageHeader) + ntohs(message->header.body_length), 0);
    if (bytes_sent < 0) {
        perror("Ошибка отправки сообщения");
        return -1;
    }
    return 0;
}