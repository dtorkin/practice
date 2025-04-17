/*
 * protocol/message_utils.c
 *
 * Описание:
 * Реализации утилитных функций для работы со структурами сообщений.
 */

#include "message_utils.h"
#include <arpa/inet.h> // Для htons, ntohs, htonl, ntohl
#include <string.h>    // Для memcpy в будущем (для массивов)

// Получить полный номер сообщения
uint16_t get_full_message_number(const MessageHeader *header) {
	// Собираем старшие биты из флагов
    uint16_t highBits = ((uint16_t)header->flags.hc_ct10p << 10) |
                        ((uint16_t)header->flags.hc_ct_bp << 9)  |
                        ((uint16_t)header->flags.hc_t_bp << 8);
    // Комбинируем с младшими битами из поля номера
	return (highBits | header->message_number);
}

// --- Функции преобразования порядка байт ---

// Вспомогательная функция для преобразования массива int16_t
static void convert_int16_array_order(int16_t *array, size_t count, uint16_t (*converter)(uint16_t)) {
    for (size_t i = 0; i < count; ++i) {
        array[i] = (int16_t)converter((uint16_t)array[i]);
    }
}

// Преобразовать в сетевой порядок
void message_to_network_byte_order(Message *message) {
    // Преобразуем длину тела
	uint16_t body_len_host = ntohs(message->header.body_length); // Сначала в хост, если уже сетевой
    message->header.body_length = htons(body_len_host);          // Затем обратно в сетевой

    // Преобразуем поля тела в зависимости от типа сообщения
    switch (message->header.message_type) {
        case MESSAGE_TYPE_CONFIRM_INIT: { // 4.2.2
            ConfirmInitBody *body = (ConfirmInitBody *)message->body;
            body->bcb = htonl(ntohl(body->bcb)); // host -> network
            break;
        }
        case MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA: { // 4.2.4
            PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody *)message->body;
            body->bcb = htonl(ntohl(body->bcb)); // host -> network
            break;
        }
		case MESSAGE_TYPE_RESULTATY_KONTROLYA: { // 4.2.6
			RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody *)message->body;
			body->vsk = htons(ntohs(body->vsk)); // host -> network
            body->bcb = htonl(ntohl(body->bcb)); // host -> network
			break;
		}
		case MESSAGE_TYPE_SOSTOYANIE_LINII: { // 4.2.8
			SostoyanieLiniiBody *body = (SostoyanieLiniiBody *)message->body;
			body->kla = htons(ntohs(body->kla)); // host -> network
            body->sla = htonl(ntohl(body->sla)); // host -> network
            body->ksa = htons(ntohs(body->ksa)); // host -> network
            body->bcb = htonl(ntohl(body->bcb)); // host -> network
			break;
		}
        case MESSAGE_TYPE_PRIYAT_PARAMETRY_SO: { // 4.2.9
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
            // uint8_t поля не требуют преобразования
            break;
        }
        // case MESSAGE_TYPE_PRIYAT_TIME_REF_RANGE: // 4.2.10 - complex_int8_t не требует
        case MESSAGE_TYPE_PRIYAT_REPER: { // 4.2.11
            PrinyatReperBody *body = (PrinyatReperBody *)message->body;
            body->NTSO1 = htons(ntohs(body->NTSO1));
            body->ReperR1 = htons(ntohs(body->ReperR1));
            body->ReperA1 = htons(ntohs(body->ReperA1));
            body->NTSO2 = htons(ntohs(body->NTSO2));
            body->ReperR2 = htons(ntohs(body->ReperR2));
            body->ReperA2 = htons(ntohs(body->ReperA2));
            body->NTSO3 = htons(ntohs(body->NTSO3));
            body->ReperR3 = htons(ntohs(body->ReperR3));
            body->ReperA3 = htons(ntohs(body->ReperA3));
            body->NTSO4 = htons(ntohs(body->NTSO4));
            body->ReperR4 = htons(ntohs(body->ReperR4));
            body->ReperA4 = htons(ntohs(body->ReperA4));
            break;
        }
        case MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR: { // 4.2.12 - базовая часть
             PrinyatParametrySdrBodyBase *body = (PrinyatParametrySdrBodyBase *)message->body;
             body->q = htons(ntohs(body->q));
             body->sigmaybm = htons(ntohs(body->sigmaybm));
             body->nfft = htons(ntohs(body->nfft));
             body->mrr = htons(ntohs(body->mrr));
             // Добавить преобразование HRR массива, если он передается вместе
             break;
         }
        case MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO: { // 4.2.13
            PrinyatParametry3TsoBody *body = (PrinyatParametry3TsoBody *)message->body;
            body->Rezerv = htons(ntohs(body->Rezerv));
            body->Ncadr = htons(ntohs(body->Ncadr));
            body->Q1 = htons(ntohs(body->Q1));
            body->Q1_OR1 = htons(ntohs(body->Q1_OR1));
            break;
        }
        case MESSAGE_TYPE_PRIYAT_REF_AZIMUTH: { // 4.2.14
            PrinyatRefAzimuthBody *body = (PrinyatRefAzimuthBody *)message->body;
            body->NTSO = htons(ntohs(body->NTSO));
            convert_int16_array_order(body->ref_azimuth, REF_AZIMUTH_SIZE, htons);
            break;
        }
        case MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD: { // 4.2.15 - базовая часть
             PrinyatParametryTsdBodyBase *body = (PrinyatParametryTsdBodyBase *)message->body;
             body->rezerv = htons(ntohs(body->rezerv));
             body->nin = htons(ntohs(body->nin));
             body->nout = htons(ntohs(body->nout));
             body->mrn = htons(ntohs(body->mrn));
             // Добавить преобразование OKM, HShMR, HAR массивов, если они передаются вместе
             break;
         }
        // case MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE: // 4.2.16 - uint8_t не требует
        case MESSAGE_TYPE_PREDUPREZHDENIE: { // 5.2
            PreduprezhdenieBody* body = (PreduprezhdenieBody*)message->body;
            body->bcb = htonl(ntohl(body->bcb));
            break;
        }
        // ... Добавить case'ы для других типов сообщений, имеющих поля uint16/uint32 ...
		default:
			// Типы сообщений без полей uint16/uint32 в теле или только с uint8/массивами
            // или еще не добавленные типы
			break;
	}
}

// Преобразовать в порядок хоста
void message_to_host_byte_order(Message *message) {
    // Преобразуем длину тела
    uint16_t body_len_net = message->header.body_length;
    message->header.body_length = ntohs(body_len_net);

    // Преобразуем поля тела в зависимости от типа сообщения
    switch (message->header.message_type) {
        case MESSAGE_TYPE_CONFIRM_INIT: { // 4.2.2
            ConfirmInitBody *body = (ConfirmInitBody *)message->body;
            body->bcb = ntohl(body->bcb); // network -> host
            break;
        }
        case MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA: { // 4.2.4
            PodtverzhdenieKontrolyaBody *body = (PodtverzhdenieKontrolyaBody *)message->body;
            body->bcb = ntohl(body->bcb); // network -> host
            break;
        }
		case MESSAGE_TYPE_RESULTATY_KONTROLYA: { // 4.2.6
			RezultatyKontrolyaBody *body = (RezultatyKontrolyaBody *)message->body;
			body->vsk = ntohs(body->vsk); // network -> host
            body->bcb = ntohl(body->bcb); // network -> host
			break;
		}
		case MESSAGE_TYPE_SOSTOYANIE_LINII: { // 4.2.8
			SostoyanieLiniiBody *body = (SostoyanieLiniiBody *)message->body;
			body->kla = ntohs(body->kla); // network -> host
            body->sla = ntohl(body->sla); // network -> host
            body->ksa = ntohs(body->ksa); // network -> host
            body->bcb = ntohl(body->bcb); // network -> host
			break;
		}
        case MESSAGE_TYPE_PRIYAT_PARAMETRY_SO: { // 4.2.9
            PrinyatParametrySoBody *body = (PrinyatParametrySoBody *)message->body;
            body->q = ntohs(body->q);
            body->knk = ntohs(body->knk);
            body->knk_or1 = ntohs(body->knk_or1);
            body->l1 = ntohs(body->l1);
            body->l2 = ntohs(body->l2);
            body->l3 = ntohs(body->l3);
            body->sigmaybm = ntohs(body->sigmaybm);
            body->rgd = ntohs(body->rgd);
            body->fixp = ntohs(body->fixp);
            break;
        }
        // case MESSAGE_TYPE_PRIYAT_TIME_REF_RANGE: // 4.2.10 - не требует
        case MESSAGE_TYPE_PRIYAT_REPER: { // 4.2.11
            PrinyatReperBody *body = (PrinyatReperBody *)message->body;
            body->NTSO1 = ntohs(body->NTSO1);
            body->ReperR1 = ntohs(body->ReperR1);
            body->ReperA1 = ntohs(body->ReperA1);
            body->NTSO2 = ntohs(body->NTSO2);
            body->ReperR2 = ntohs(body->ReperR2);
            body->ReperA2 = ntohs(body->ReperA2);
            body->NTSO3 = ntohs(body->NTSO3);
            body->ReperR3 = ntohs(body->ReperR3);
            body->ReperA3 = ntohs(body->ReperA3);
            body->NTSO4 = ntohs(body->NTSO4);
            body->ReperR4 = ntohs(body->ReperR4);
            body->ReperA4 = ntohs(body->ReperA4);
            break;
        }
        case MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR: { // 4.2.12 - базовая часть
             PrinyatParametrySdrBodyBase *body = (PrinyatParametrySdrBodyBase *)message->body;
             body->q = ntohs(body->q);
             body->sigmaybm = ntohs(body->sigmaybm);
             body->nfft = ntohs(body->nfft);
             body->mrr = ntohs(body->mrr);
             // Добавить преобразование HRR массива, если он читается
             break;
         }
        case MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO: { // 4.2.13
            PrinyatParametry3TsoBody *body = (PrinyatParametry3TsoBody *)message->body;
            body->Rezerv = ntohs(body->Rezerv);
            body->Ncadr = ntohs(body->Ncadr);
            body->Q1 = ntohs(body->Q1);
            body->Q1_OR1 = ntohs(body->Q1_OR1);
            break;
        }
        case MESSAGE_TYPE_PRIYAT_REF_AZIMUTH: { // 4.2.14
            PrinyatRefAzimuthBody *body = (PrinyatRefAzimuthBody *)message->body;
            body->NTSO = ntohs(body->NTSO);
            convert_int16_array_order(body->ref_azimuth, REF_AZIMUTH_SIZE, ntohs);
            break;
        }
        case MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD: { // 4.2.15 - базовая часть
             PrinyatParametryTsdBodyBase *body = (PrinyatParametryTsdBodyBase *)message->body;
             body->rezerv = ntohs(body->rezerv);
             body->nin = ntohs(body->nin);
             body->nout = ntohs(body->nout);
             body->mrn = ntohs(body->mrn);
             // Добавить преобразование OKM, HShMR, HAR массивов, если они читаются
             break;
         }
        // case MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE: // 4.2.16 - не требует
        case MESSAGE_TYPE_PREDUPREZHDENIE: { // 5.2
            PreduprezhdenieBody* body = (PreduprezhdenieBody*)message->body;
            body->bcb = ntohl(body->bcb);
            break;
        }
		// ... Добавить case'ы для других типов сообщений ...
		default:
			// Типы без полей uint16/uint32 или еще не добавленные
			break;
	}
}