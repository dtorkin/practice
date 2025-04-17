/*
 * protocol/message_builder.c
 *
 * Описание:
 * Реализации функций для создания различных типов сообщений протокола.
 */

#include "message_builder.h"
#include <string.h>     // Для memset
#include <arpa/inet.h>  // Для htons, htonl

// Макрос для установки заголовка (упрощение)
#define SET_HEADER(msg, target_addr, direction, msg_num, msg_type, body_struct_type) \
    do { \
        memset(&(msg), 0, sizeof(Message)); \
        (msg).header.address = (target_addr); \
        (msg).header.flags.np = (direction); \
        (msg).header.flags.hc_t_bp = ((msg_num) >> 8) & 0x01; \
        (msg).header.flags.hc_ct_bp = ((msg_num) >> 9) & 0x01; \
        (msg).header.flags.hc_ct10p = ((msg_num) >> 10) & 0x01; \
        (msg).header.body_length = htons(sizeof(body_struct_type)); \
        (msg).header.message_number = (msg_num) & 0xFF; \
        (msg).header.message_type = (msg_type); \
    } while (0)

// Макрос для установки заголовка без тела
#define SET_HEADER_NO_BODY(msg, target_addr, direction, msg_num, msg_type) \
    do { \
        memset(&(msg), 0, sizeof(Message)); \
        (msg).header.address = (target_addr); \
        (msg).header.flags.np = (direction); \
        (msg).header.flags.hc_t_bp = ((msg_num) >> 8) & 0x01; \
        (msg).header.flags.hc_ct_bp = ((msg_num) >> 9) & 0x01; \
        (msg).header.flags.hc_ct10p = ((msg_num) >> 10) & 0x01; \
        (msg).header.body_length = htons(0); \
        (msg).header.message_number = (msg_num) & 0xFF; \
        (msg).header.message_type = (msg_type); \
    } while (0)


// --- Реализации функций создания сообщений (От УВМ к СВ-М) ---

// [4.2.1] «Инициализация канала»
Message create_init_channel_message(LogicalAddress uvm_address, LogicalAddress svm_address, uint16_t message_num) {
	Message message;
	SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_INIT_CHANNEL, InitChannelBody);
	InitChannelBody *body = (InitChannelBody *) message.body;
	body->lauvm = uvm_address;
	body->lak = svm_address; // Адрес СВМ, который нужно установить
	return message;
}

// [4.2.3] «Провести контроль»
Message create_provesti_kontrol_message(LogicalAddress svm_address, uint8_t tk, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PROVESTI_KONTROL, ProvestiKontrolBody);
	ProvestiKontrolBody *body = (ProvestiKontrolBody *) message.body;
	body->tk = tk;
	return message;
}

// [4.2.5] «Выдать результаты контроля»
Message create_vydat_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t vpk, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA, VydatRezultatyKontrolyaBody);
    VydatRezultatyKontrolyaBody *body = (VydatRezultatyKontrolyaBody *) message.body;
    body->vrk = vpk;
    return message;
}

// [4.2.7] «Выдать состояние линии»
Message create_vydat_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    // Тело пустое, используем SET_HEADER_NO_BODY
    SET_HEADER_NO_BODY(message, svm_address, 0, message_num, MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII);
    return message;
}

// [4.2.9] «Принять параметры СО»
Message create_prinyat_parametry_so_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_PARAMETRY_SO, PrinyatParametrySoBody);
	PrinyatParametrySoBody *body = (PrinyatParametrySoBody *) message.body;

	// Инициализируем поля тестовыми данными (как в common.c)
	body->pp = MODE_VR;	   // Пример: Режим VR
	body->brl = 0x07;		 // Пример: Маска бланкирования
	body->q0 = 0x03;		  // Пример: Порог помехи
	body->q = htons(1500);	 // Пример: Нормализованная константа шума
	body->knk = htons(300);	  // Пример: KNK
	body->knk_or1 = htons(350);  // Пример: KNK_OR1
	for (int i = 0; i < 23; ++i) {
		body->weight[i] = 10 + i;
	}
	body->l1 = htons(100);	   // Пример: Длина опоры L1
	body->l2 = htons(150);	   // Пример: Длина опоры L2
	body->l3 = htons(200);	   // Пример: Длина опоры L3
	body->aru = 0x01;		 // Пример: Режим АРУ (1 - измерительный)
	body->karu = 0x0A;		// Пример: KARU
	body->sigmaybm = htons(2500); // Пример: SIGMAYBM
	body->rgd = htons(1024);	// Пример: Длина строки РГД
	body->yo = 0x01;		  // Пример: Уровень обработки
	body->a2 = 0x05;		  // Пример: A2
	body->fixp = htons(100);	// Пример: Уровень фиксированного порога
	return message;
}

// [4.2.10] «Принять TIME_REF_RANGE»
Message create_prinyat_time_ref_range_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_TIME_REF_RANGE, PrinyatTimeRefRangeBody);
	PrinyatTimeRefRangeBody *body = (PrinyatTimeRefRangeBody *) message.body;
	// Пример: Заполнение массива комплексных чисел
	for (int i = 0; i < TIME_REF_RANGE_ELEMENTS; ++i) {
		body->time_ref_range[i].imag = (int8_t)(i % 128);
		body->time_ref_range[i].real = (int8_t)((i + 64) % 128);
	}
	return message;
}

// [4.2.11] «Принять Reper»
Message create_prinyat_reper_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_REPER, PrinyatReperBody);
	PrinyatReperBody *body = (PrinyatReperBody *) message.body;
	// Пример: Заполнение реперных точек
	body->NTSO1 = htons(10);
	body->ReperR1 = htons(1000);
	body->ReperA1 = htons(2000);
	body->NTSO2 = htons(11);
	body->ReperR2 = htons(1100);
	body->ReperA2 = htons(2100);
	body->NTSO3 = htons(12);
	body->ReperR3 = htons(1200);
	body->ReperA3 = htons(2200);
	body->NTSO4 = htons(13);
	body->ReperR4 = htons(1300);
	body->ReperA4 = htons(2300);
	return message;
}

// [4.2.12] «Принять параметры СДР»
// ВАЖНО: Эта функция создает сообщение ТОЛЬКО с базовой частью.
// Массив HRR должен быть добавлен отдельно перед отправкой,
// а header.body_length скорректирован!
Message create_prinyat_parametry_sdr_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    // Устанавливаем заголовок с размером БАЗОВОЙ структуры
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR, PrinyatParametrySdrBodyBase);
	PrinyatParametrySdrBodyBase *body = (PrinyatParametrySdrBodyBase *) message.body;

    // Заполняем базовую часть
	body->pp_nl = 0x01; // Пример
	body->brl = 0x07;   // Пример
	body->kdec = 0x02;  // Пример
	body->yo = 0x01;    // Пример
	body->sland = 0x10; // Пример
	body->sf = 0x05;    // Пример
	body->t0 = 0x20;    // Пример
	body->t1 = 0x15;    // Пример
	body->q0 = 0x03;    // Пример
	body->q = htons(1500); // Пример
	body->aru = 0x01;   // Пример
	body->karu = 0x0A;  // Пример
	body->sigmaybm = htons(2500); // Пример
	body->kw = 0x01;    // Пример
	for (int i = 0; i < 23; ++i) {
		body->w[i] = 0x0F + i; // Пример
	}
	body->nfft = htons(128); // Пример
	body->or_param = 0x08; // Пример
	body->oa = 0x0A;     // Пример
	body->mrr = htons(500); // !!! Пример MRR. Это значение должно быть реальным для расчета и добавления HRR!
	// body->fixp был пропущен в таблице 4.27, но есть в 4.22 - добавляем
    // body->fixp = htons(100); // Пример
	return message;
    // ПРИМЕЧАНИЕ: Перед отправкой нужно будет:
    // 1. Определить реальный размер MRR.
    // 2. Выделить память или использовать существующий буфер для HRR[MRR].
    // 3. Скопировать данные HRR в message.body + sizeof(PrinyatParametrySdrBodyBase).
    // 4. Обновить message.header.body_length = htons(sizeof(PrinyatParametrySdrBodyBase) + mrr * sizeof(complex_fixed16_t)).
}


// [4.2.13] «Принять параметры 3ЦО»
Message create_prinyat_parametry_3tso_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO, PrinyatParametry3TsoBody);
	PrinyatParametry3TsoBody *body = (PrinyatParametry3TsoBody *) message.body;

	// Инициализируем поля тестовыми данными
	body->Rezerv = htons(0);
	body->Ncadr = htons(1024);
	body->Xnum = 128;
	for (int i = 0; i < 8; ++i) {
		for (int j = 0; j < 16; ++j) {
			body->DNA[i][j] = (uint8_t)(i * 16 + j);
			body->DNA_INVERS[i][j] = (uint8_t)(255 - (i * 16 + j));
		}
	}
	for (int i = 0; i < 16; ++i) {
		body->DNA_OR1[i] = (uint8_t)(i + 10);
		body->DNA_INVERS_OR1[i] = (uint8_t)(255 - (i + 10));
		body->Sea_or_land[i] = (i % 2 == 0) ? 0xFF : 0x00;
	}
	body->Q1 = htons(500);
	body->Q1_OR1 = htons(600);
	body->Part = 0x80;
	return message;
}

// [4.2.14] «Принять REF_AZIMUTH»
Message create_prinyat_ref_azimuth_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_REF_AZIMUTH, PrinyatRefAzimuthBody);
	PrinyatRefAzimuthBody *body = (PrinyatRefAzimuthBody *) message.body;
	body->NTSO = htons(20); // Пример номера цикла
	// Пример: Заполнение массива int16_t
	for (size_t i = 0; i < REF_AZIMUTH_SIZE; ++i) {
		body->ref_azimuth[i] = htons((int16_t)(i % 32768 - 16384)); // Пример значений
	}
	return message;
}

// [4.2.15] «Принять параметры ЦДР»
// ВАЖНО: Создает сообщение только с базовой частью.
// Массивы OKM, HShMR, HAR должны быть добавлены отдельно.
Message create_prinyat_parametry_tsd_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD, PrinyatParametryTsdBodyBase);
	PrinyatParametryTsdBodyBase *body = (PrinyatParametryTsdBodyBase *) message.body;

	// Заполняем базовую часть
    body->rezerv = htons(0);    // Пример
    body->nin = htons(256);     // !!! Пример Nin. Должно быть реальным.
    body->nout = htons(128);    // !!! Пример Nout. Должно быть реальным.
    body->mrn = htons(64);      // Пример
    body->shmr = 10;            // Пример
    body->nar = htons(32);       // !!! Пример NAR. Должно быть реальным.

	return message;
    // ПРИМЕЧАНИЕ: Перед отправкой нужно будет:
    // 1. Определить реальные размеры Nin, Nout, NAR.
    // 2. Выделить память или использовать буферы для OKM[Nout], HShMR[Nin], HAR[NAR, Nin].
    // 3. Скопировать данные массивов в message.body + sizeof(PrinyatParametryTsdBodyBase).
    // 4. Обновить message.header.body_length = htons(sizeof(PrinyatParametryTsdBodyBase) +
    //                                              nout*sizeof(int8) + nin*sizeof(uint8) +
    //                                              nar*nin*sizeof(complex_fixed16_t)).
}

// [4.2.16] «Навигационные данные»
Message create_navigatsionnye_dannye_message(LogicalAddress svm_address, uint16_t message_num) {
    Message message;
    SET_HEADER(message, svm_address, 0, message_num, MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE, NavigatsionnyeDannyeBody);
	NavigatsionnyeDannyeBody *body = (NavigatsionnyeDannyeBody *) message.body;
	// Заполнение массива mnd[] примерами
	for (int i = 0; i < NAV_DATA_SIZE; ++i) {
		body->mnd[i] = i; // Простые значения для примера
	}
	return message;
}


// --- Реализации функций создания сообщений (От СВ-М к УВМ) ---

// [4.2.2] «Подтверждение инициализации канала»
Message create_confirm_init_message(LogicalAddress svm_address, uint8_t slp, uint8_t vdr, uint8_t bop1, uint8_t bop2, uint32_t bcb, uint16_t message_num) {
    Message message;
    // Цель - УВМ, Направление - 1 (от СВМ)
    SET_HEADER(message, LOGICAL_ADDRESS_UVM, 1, message_num, MESSAGE_TYPE_CONFIRM_INIT, ConfirmInitBody);
	ConfirmInitBody *body = (ConfirmInitBody *) message.body;
	body->lak = svm_address; // Подтверждаем адрес СВМ
	body->slp = slp;
	body->vdr = vdr;
	body->bop1 = bop1;
	body->bop2 = bop2;
	body->bcb = htonl(bcb); // Преобразуем BCB в сетевой порядок
	return message;
}

// [4.2.4] «Подтверждение контроля»
Message create_podtverzhdenie_kontrolya_message(LogicalAddress svm_address, uint8_t tk, uint32_t bcb, uint16_t message_num) {
    Message message;
    SET_HEADER(message, LOGICAL_ADDRESS_UVM, 1, message_num, MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA, PodtverzhdenieKontrolyaBody);
    PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody *) message.body;
    body->lak = svm_address;
    body->tk = tk; // Возвращаем тип контроля из запроса
    body->bcb = htonl(bcb);
    return message;
}

// [4.2.6] «Результаты контроля»
Message create_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t rsk, uint16_t vsk, uint32_t bcb, uint16_t message_num) {
    Message message;
    SET_HEADER(message, LOGICAL_ADDRESS_UVM, 1, message_num, MESSAGE_TYPE_RESULTATY_KONTROLYA, RezultatyKontrolyaBody);
    RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody *) message.body;
    body->lak = svm_address;
    body->rsk = rsk;
    body->vsk = htons(vsk); // Преобразуем время в сетевой порядок
    body->bcb = htonl(bcb);
    return message;
}

// [4.2.8] «Состояние линии»
Message create_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t kla, uint32_t sla, uint16_t ksa, uint32_t bcb, uint16_t message_num) {
    Message message;
    SET_HEADER(message, LOGICAL_ADDRESS_UVM, 1, message_num, MESSAGE_TYPE_SOSTOYANIE_LINII, SostoyanieLiniiBody);
	SostoyanieLiniiBody *body = (SostoyanieLiniiBody *) message.body;
	body->lak = svm_address;
	body->kla = htons(kla);
	body->sla = htonl(sla); // Время в 1/100 мкс
	body->ksa = htons(ksa);
	body->bcb = htonl(bcb);
	return message;
}

// ... Реализации для create_subk_message, create_ko_message и т.д. ...

// [5.2] Сообщение "Предупреждение"
Message create_preduprezhdenie_message(LogicalAddress svm_address, uint8_t tks, const uint8_t* pks, uint32_t bcb, uint16_t message_num) {
    Message message;
    SET_HEADER(message, LOGICAL_ADDRESS_UVM, 1, message_num, MESSAGE_TYPE_PREDUPREZHDENIE, PreduprezhdenieBody);
    PreduprezhdenieBody *body = (PreduprezhdenieBody *) message.body;
    body->lak = svm_address;
    body->tks = tks;
    memcpy(body->pks, pks, sizeof(body->pks)); // Копируем 6 байт параметров
    body->bcb = htonl(bcb);
    return message;
}