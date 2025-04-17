#include "common.h"
#include <string.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h> // Добавляем для errno

// Функция: Получить полный номер сообщения (на основе флагов и номера сообщения)
uint16_t get_full_message_number(const MessageHeader *header) {
    uint16_t highBits = (header->flags.hc_t_bp << 8) | (header->flags.hc_ct_bp << 9) | (header->flags.hc_ct10p << 10);
    return (highBits | header->message_number);
}

// Функция: Создать сообщение "Инициализация канала" (Пункт 4.2.1)
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

    InitChannelBody *body = (InitChannelBody *)message.body;
    body->lauvm = uvm_address;
    body->lak = svm_address;

    return message;
}

// Функция: Создать сообщение "Подтверждение инициализации" (Пункт 4.2.2)
Message create_confirm_init_message(LogicalAddress svm_address, uint8_t slp, uint8_t vdr, uint8_t vor1, uint8_t vor2, uint32_t bcb, uint16_t message_num) {
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

    ConfirmInitBody* body = (ConfirmInitBody*)message.body;
    body->lak = svm_address;
    body->slp = slp;
    body->vdr = vdr;
    body->vor1 = vor1;
    body->vor2 = vor2;
    body->bcb = htonl(bcb);

    return message;
}

// Функция: Создать сообщение "Провести контроль" (Пункт 4.2.3)
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

    ProvestiKontrolBody *body = (ProvestiKontrolBody *)message.body;
    body->tk = tk;

    return message;
}

// Функция: Создать сообщение "Подтверждение контроля" (Пункт 4.2.4)
Message create_podtverzhdenie_kontrolya_message(LogicalAddress svm_address, uint8_t tk, uint32_t bcb, uint16_t message_num) {
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

    PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody *)message.body;
    body->lak = svm_address;
    body->tk = tk;
    body->bcb = htonl(bcb);

    return message;
}

// Функция: Создать сообщение "Выдать результаты контроля" (Пункт 4.2.5)
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

    VydatRezultatyKontrolyaBody *body = (VydatRezultatyKontrolyaBody *)message.body;
    body->vpk = vpk;

    return message;
}

// Функция: Создать сообщение "Результаты контроля" (Пункт 4.2.6)
Message create_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t rsk, uint16_t bck, uint32_t bcb, uint16_t message_num) {
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

    RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody *)message.body;
    body->lak = svm_address;
    body->rsk = rsk;
    body->bck = htons(bck);
    body->bcb = htonl(bcb);

    return message;
}

// Функция: Создать сообщение "Выдать состояние линии" (Пункт 4.2.7)
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

// Функция: Создать сообщение "Состояние линии" (Пункт 4.2.8)
Message create_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t kla, uint32_t sla, uint16_t ksa, uint32_t bcb, uint16_t message_num) {
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

    SostoyanieLiniiBody *body = (SostoyanieLiniiBody *)message.body;
    body->lak = svm_address;
    body->kla = htons(kla);
    body->sla = htonl(sla);
    body->ksa = htons(ksa);
    body->bcb = htonl(bcb);

    return message;
}

// Функция: Создать сообщение "Состояние линии" тип 136 - Заглушка для 4.2.6 (Заглушка, не описано в документе)
Message create_sostoyanie_linii_136_message(LogicalAddress svm_address, uint32_t bcb, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 1;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(SostoyanieLinii136Body));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_SOSTOYANIE_LINII_136;

    SostoyanieLinii136Body *body = (SostoyanieLinii136Body *)message.body;
    body->lak = svm_address;
    body->bcb = htonl(bcb);

    return message;
}

// Функция: Создать сообщение "Выдать состояние линии" тип 137 - Заглушка для 4.2.7 (Заглушка, не описано в документе)
Message create_vydat_sostoyanie_linii_137_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 0;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(0); // Пустое тело
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII_137;

    return message;
}

// Функция: Создать сообщение "Состояние линии" тип 138 - Заглушка для 4.2.8 (Заглушка, не описано в документе)
Message create_sostoyanie_linii_138_message(LogicalAddress svm_address, uint32_t bcb, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 1;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(SostoyanieLinii138Body));
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_SOSTOYANIE_LINII_138;

    SostoyanieLinii138Body *body = (SostoyanieLinii138Body *)message.body;
    body->lak = svm_address;
    body->bcb = htonl(bcb);

    return message;
}

// Функция: Создать сообщение "Принять параметры СДР" (Пункт 4.2.12)
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

    PrinyatParametrySdrBody *body = (PrinyatParametrySdrBody *)message.body;
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
    body->sigmaybm = htons(2500); // Пример константы номинального среднеквадратичного уровня шума на выходе устройства МПУ 11B521-4 (SIGMAYBM)
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

// --- НОВОЕ: Функция создания сообщения "Принять параметры ЦДР" (Пункт 4.2.15) ---
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
    message.header.message_type = MESSAGE_TYPE_PRIYAT_PARAMETRY_TSDR; // Используем добавленный тип

    PrinyatParametryTsdBody *body = (PrinyatParametryTsdBody *)message.body;
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

// --- НОВОЕ: Функция создания сообщения "Навигационные данные" (Пункт 4.2.16) ---
Message create_navigatsionnye_dannye_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    memset(&message, 0, sizeof(Message));

    message.header.address = svm_address;
    message.header.flags.np = 0;
    message.header.flags.hc_t_bp = (message_num >> 8) & 0x01;
    message.header.flags.hc_ct_bp = (message_num >> 9) & 0x01;
    message.header.flags.hc_ct10p = (message_num >> 10) & 0x01;
    message.header.body_length = htons(sizeof(NavigatsionnyeDannyeBody)); // Используем структуру NavigatsionnyeDannyeBody
    message.header.message_number = message_num & 0xFF;
    message.header.message_type = MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE; // Используем добавленный тип

    NavigatsionnyeDannyeBody *body = (NavigatsionnyeDannyeBody *)message.body;
    // Заполнение массива mnd[] примерами навигационных данных (в реальности - данные)
    for (int i = 0; i < 256; ++i) {
        body->mnd[i] = i; // Простые значения для примера
    }

    return message;
}


// Функция: Преобразовать сообщение в сетевой порядок байтов (Network Byte Order)
void message_to_network_byte_order(Message *message) {
    message->header.body_length = htons(message->header.body_length);
     if (message->header.message_type == MESSAGE_TYPE_RESULTATY_KONTROLYA) {
        RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody *)message->body;
        body->bck = htons(body->bck);
    } else if (message->header.message_type == MESSAGE_TYPE_SOSTOYANIE_LINII) {
        SostoyanieLiniiBody *body = (SostoyanieLiniiBody *)message->body;
        body->kla = htons(body->kla);
        body->ksa = htons(body->ksa);
    }  else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR) {
        PrinyatParametrySdrBody *body = (PrinyatParametrySdrBody *)message->body;
        body->q = htons(body->q);
        body->sigmaybm = htons(body->sigmaybm);
        body->nfft = htons(body->nfft);
        body->mrr = htons(body->mrr);
        body->fixp = htons(body->fixp);
    }  // --- НОВОЕ: Добавление обработки для "Принять параметры ЦДР" ---
    else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_TSDR) {
        PrinyatParametryTsdBody *body = (PrinyatParametryTsdBody *)message->body;
        body->nin = htons(body->nin);
        body->nout = htons(body->nout);
        body->mrn = htons(body->mrn);
        body->rezerv = htons(body->rezerv); // Хотя rezerv не используется, для примера преобразуем
    }
}

// Функция: Преобразовать сообщение в порядок байтов хоста (Host Byte Order)
void message_to_host_byte_order(Message *message) {
    message->header.body_length = ntohs(message->header.body_length);
    if (message->header.message_type == MESSAGE_TYPE_CONFIRM_INIT) {
        ConfirmInitBody *body = (ConfirmInitBody *)message->body;
        body->bcb = ntohl(body->bcb);
    } else if (message->header.message_type == MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA) {
        PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody *)message->body;
        body->bcb = ntohl(body->bcb);
    } else if (message->header.message_type == MESSAGE_TYPE_RESULTATY_KONTROLYA) {
        RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody *)message->body;
        body->bck = ntohs(body->bck);
        body->bcb = ntohl(body->bcb);
    } else if (message->header.message_type == MESSAGE_TYPE_SOSTOYANIE_LINII) {
        SostoyanieLiniiBody *body = (SostoyanieLiniiBody *)message->body;
        body->kla = ntohs(body->kla);
        body->sla = ntohl(body->sla);
        body->ksa = ntohs(body->ksa);
        body->bcb = ntohl(body->bcb);
    } else if (message->header.message_type == MESSAGE_TYPE_SOSTOYANIE_LINII_136) {
        SostoyanieLinii136Body *body = (SostoyanieLinii136Body *)message->body;
        body->bcb = ntohl(body->bcb);
    } else if (message->header.message_type == MESSAGE_TYPE_SOSTOYANIE_LINII_138) {
        SostoyanieLinii138Body *body = (SostoyanieLinii138Body *)message->body;
        body->bcb = ntohl(body->bcb);
    }  else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR) {
        PrinyatParametrySdrBody *body = (PrinyatParametrySdrBody *)message->body;
        body->q = ntohs(body->q);
        body->sigmaybm = ntohs(body->sigmaybm);
        body->nfft = ntohs(body->nfft);
        body->mrr = ntohs(body->mrr);
        body->fixp = ntohs(body->fixp);
    } // --- НОВОЕ: Добавление обработки для "Принять параметры ЦДР" ---
     else if (message->header.message_type == MESSAGE_TYPE_PRIYAT_PARAMETRY_TSDR) {
        PrinyatParametryTsdBody *body = (PrinyatParametryTsdBody *)message->body;
        body->nin = ntohs(body->nin);
        body->nout = ntohs(body->nout);
        body->mrn = ntohs(body->mrn);
        body->rezerv = ntohs(body->rezerv); // Хотя rezerv не используется, для примера преобразуем
    }
}

// Функция: Отправить сообщение через сокет
int send_message(int socketFD, Message *message) {
    message_to_network_byte_order(message);
    ssize_t bytes_sent = send(socketFD, message, sizeof(MessageHeader) + ntohs(message->header.body_length), 0);
    if (bytes_sent < 0) {
        perror("Ошибка отправки сообщения");
        return -1; // Возвращаем код ошибки
    }
    return 0; // Возвращаем код успеха
}