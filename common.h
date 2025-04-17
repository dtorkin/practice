// common.h

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

// Константы
#define MAX_MESSAGE_BODY_SIZE 65522 // Максимальный размер тела сообщения (из раздела 4.1)
#define LOGICAL_ADDRESS_UVM			  0x01 // Логический адрес УВМ (из таблицы 4.2)
#define LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1 0x08 // Пример логического адреса СВ-М (из таблицы 4.2)

// Режимы работы РСА
typedef enum {
	MODE_OR,   // Режим OP
	MODE_OR1,  // Режим OP1
	MODE_DR,   // Режим ДР
	MODE_VR	// Режим ВР
} RadarMode;

// Состояние SVM (SVM State)
typedef enum {
	STATE_NOT_INITIALIZED, // Не инициализировано
	STATE_INITIALIZED,	 // Инициализировано
	STATE_SELF_TEST		 // Самоконтроль
} SVMState;

// Определения для комплексных чисел
typedef struct {
	int8_t imag; // Мнимая часть (младший байт)
	int8_t real; // Действительная часть (старший байт)
} complex_int8_t;

typedef struct {
	int16_t imag; // Мнимая часть (младшие 2 байта)
	int16_t real; // Действительная часть (старшие 2 байта)
} complex_fixed16_t;

/********************************************************************************/
/*				  СТРУКТУРА И ТИПЫ СООБЩЕНИЙ (MESSAGE_TYPE_)				  */
/********************************************************************************/

// [Таблица 4.3] Флаги
typedef struct {
	uint8_t np: 1;	   // Направление передачи (0 - от УВМ к СВ-М, 1 - от СВ-М к УВМ)
	uint8_t hc_t_bp: 1;  // Восьмой разряд номера сообщения
	uint8_t hc_ct_bp: 1; // Девятый разряд номера сообщения
	uint8_t hc_ct10p: 1; // Десятый разряд номера сообщения
	uint8_t reserve4: 1; // Резерв
	uint8_t reserve5: 1; // Резерв
	uint8_t reserve6: 1; // Резерв
	uint8_t reserve7: 1; // Резерв
} MessageFlags;

// [Таблица 4.1] Заголовок сообщения
typedef struct {
	uint8_t address;		 // Адрес (1 байт)
	MessageFlags flags;	  // Флаги (1 байт)
	uint16_t body_length;	// Длина тела сообщения (2 байта)
	uint8_t message_number;  // Номер сообщения (1 байт, младшие 8 бит номера)
	uint8_t message_type;	// Тип сообщения (1 байт)
} MessageHeader;

// [Таблица 4.1] Общая структура сообщения
typedef struct {
	MessageHeader header;
	uint8_t body[MAX_MESSAGE_BODY_SIZE]; // Тело сообщения (максимальный размер)
} Message;

// [Таблица 4.2] Логические адреса
typedef enum {
	LOGICAL_ADDRESS_UVM_VAL = LOGICAL_ADDRESS_UVM,
	LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1_VAL = LOGICAL_ADDRESS_SVM_PB_BZ_CHANNEL_1,
} LogicalAddress;

// [Таблица 4.4] Типы сообщений
typedef enum {
	// --- От УВМ к СВ-М ---
	MESSAGE_TYPE_INIT_CHANNEL = 128, // 4.2.1. «Инициализация канала»
	MESSAGE_TYPE_PROVESTI_KONTROL = 1, // 4.2.3. «Провести контроль»
	MESSAGE_TYPE_VYDAT_RESULTATY_KONTROLYA = 2, // 4.2.5. «Выдать результаты контроля»
	MESSAGE_TYPE_VYDAT_SOSTOYANIE_LINII = 6, // 4.2.7. «Выдать состояние линии»
	MESSAGE_TYPE_PRIYAT_PARAMETRY_SO = 160, // 4.2.9. «Принять параметры СО»
	MESSAGE_TYPE_PRIYAT_TIME_REF_RANGE = 161, // 4.2.10. «Принять TIME_REF_RANGE»
	MESSAGE_TYPE_PRIYAT_REPER = 162,		  // 4.2.11. «Принять Reper» 
	MESSAGE_TYPE_PRIYAT_PARAMETRY_SDR = 170, // 4.2.12. «Принять параметры СДР»
	MESSAGE_TYPE_PRIYAT_PARAMETRY_3TSO = 200, // 4.2.13. «Принять параметры 3ЦО»
	MESSAGE_TYPE_PRIYAT_REF_AZIMUTH = 201,	// 4.2.14. «Принять REF_AZIMUTH»
	MESSAGE_TYPE_PRIYAT_PARAMETRY_TSD = 210, // 4.2.15. «Принять параметры ЦДР»
	MESSAGE_TYPE_NAVIGATSIONNYE_DANNYE = 255,  // 4.2.16. «Навигационные данные»

	// --- От СВ-М к УВМ ---
	MESSAGE_TYPE_CONFIRM_INIT = 129, // 4.2.2. «Подтверждение инициализации канала»
	MESSAGE_TYPE_PODTVERZHDENIE_KONTROLYA = 3, // 4.2.4. «Подтверждение контроля»
	MESSAGE_TYPE_RESULTATY_KONTROLYA = 4, // 4.2.6. «Результаты контроля»
	MESSAGE_TYPE_SOSTOYANIE_LINII = 7, // 4.2.8. «Состояние линии»
	// TODO: 4.2.17. «СУБК»
	// TODO: 4.2.18. «КО»
	// TODO: 4.2.19. «Строка голограммы СУБК»
	// TODO: 4.2.20. «Строка радиоголограммы ДР»
	// TODO: 4.2.21. «Строка К3»
	// TODO: 4.2.22. «Строка изображения К4»
	// TODO: 4.2.23. «НК»
	// TODO: 4.2.24. «Помеха»
	// TODO: 4.2.25. «Результат ОР1»
	// TODO: 4.2.26. «РО»
	// TODO: 4.2.27. «НКДР»
	// TODO: 4.2.28. «Предупреждение»
} MessageType;

/********************************************************************************/
/*							 ТЕЛА СООБЩЕНИЙ (Body)							 */
/********************************************************************************/

// [4.2.1] «Инициализация канала» (тело сообщения)
typedef struct {
	uint8_t lauvm; // Логический адрес УВМ
	uint8_t lak;   // Логический адрес СВ-М
} InitChannelBody;

// [4.2.2] «Подтверждение инициализации канала» (тело сообщения)
typedef struct {
	uint8_t lak;   // Логический адрес СВ-М
	uint8_t slp;   // Состояние линий передач СВ-М (СЛП)
	uint8_t vdr;   // Версия прошивки ПЛИС модуля МОДР (ВДР)
	uint8_t vor1;  // Версия прошивки ПЛИС модуля МОСВ1 (BOP1)
	uint8_t vor2;  // Версия прошивки ПЛИС модуля МОСВ2 (BOP2)
	uint32_t bcb;  // Состояние BCB
} ConfirmInitBody;

// [4.2.3] «Провести контроль» (тело сообщения)
typedef struct {
	uint8_t tk; // Тип контроля (ТК)
} ProvestiKontrolBody;

// [4.2.4] «Подтверждение контроля» (тело сообщения)
typedef struct {
	uint8_t lak;  // Логический адрес СВ-М (ЛАК)
	uint8_t tk;   // Тип контроля (ТК) - из запроса
	uint32_t bcb; // Состояние ВСВ (BCB)
} PodtverzhdenieKontrolyaBody;

// [4.2.5] «Выдать результаты контроля» (тело сообщения)
typedef struct {
	uint8_t vpk; // Вид запроса выдачи результатов контроля (ВРК)
} VydatRezultatyKontrolyaBody;

// [4.2.6] «Результаты контроля» (тело сообщения)
typedef struct {
	uint8_t lak;   // Логический адрес СВ-М (ЛАК)
	uint8_t rsk;	 // Результаты контроля СВ-М (РСК)
	uint16_t bck;	// Время прохождения самоконтроля (ВСК)
	uint32_t bcb;	// Состояние ВСВ (BCB)
} RezultatyKontrolyaBody;

// [4.2.7] «Выдать состояние линии» (тело сообщения)
typedef struct {
	// Тело сообщения "Выдать состояние линии" - пустое
} VydatSostoyanieLiniiBody;

// [4.2.8] «Состояние линии» (тело сообщения)
typedef struct {
	uint8_t lak;	// Логический адрес СВ-М (ЛАК)
	uint16_t kla;   // Количество изменений состояния LinkUp (КЛА)
	uint32_t sla;   // Состояние счётчика интегрального времени LinkUp (СЛА)
	uint16_t ksa;   // Количество изменений состояния SignDet (КСА)
	uint32_t bcb;   // Состояние ВСВ (BCB)
} SostoyanieLiniiBody;

// [4.2.9] «Принять параметры СО» (тело сообщения)
typedef struct {
	uint8_t pp;	   // Режим работы РСА (PP)
	uint8_t brl;	  // Маска бланкирования рабочих лучей (БРЛ)
	uint8_t q0;	   // Пороговая константа обнаружения активной помехи (Qo)
	uint16_t q;		// Нормализованная константа для сигмы шума (Q)
	uint16_t knk;	  // Нормированная константа порога обнаружения ОР (KNK)
	uint16_t knk_or1;  // Нормированная константа порога обнаружения ОР1 (KNK_OR1)
	uint8_t weight[23]; // Массив коэффициентов взвешивающего фильтра (Weight). Спецификация говорит fixed8, используем uint8_t для байтового представления.
	uint16_t l1;	   // Длина опоры свертки по дальности для СУБК первого луча (L1)
	uint16_t l2;	   // Длина опоры свертки по дальности для СУБК второго луча (L2)
	uint16_t l3;	   // Длина опоры свертки по дальности для СУБК третьего луча (L3)
	uint8_t aru;	  // Режим работы АРУ устройства МПУ 11B521-4 (ARU) (битовое поле, используем uint8_t)
	uint8_t karu;	 // Константа значения кода аттенюации устр-ва МПУ 11B521-4 в режиме внешнего кода АРУ (KARU)
	uint16_t sigmaybm; // Константа номинального среднеквадратичного уровня шума на выходе устройства МПУ 11B521-4 (SIGMAYBM)
	uint16_t rgd;	  // Длина строки радиоголограммы в байтах (РГД)
	uint8_t yo;	   // Уровень обработки (УО)
	uint8_t a2;	   // Коэффициент допустимого порога (А2) - было 'az'
	uint16_t fixp;	 // Уровень фиксированного порога (FixP)
} PrinyatParametrySoBody;

// [4.2.10] «Принять TIME_REF_RANGE» (тело сообщения) - Таблица 4.23
typedef struct {
	complex_int8_t time_ref_range[400]; // Массив опоры по дальности (800 байт)
} PrinyatTimeRefRangeBody;

// [4.2.11] «Принять Reper» (тело сообщения) - Таблица 4.25
typedef struct {
	uint16_t NTSO1;	// Номер цикла обзора для реперной точки 1
	uint16_t ReperR1;  // Координаты по дальности реперной точки 1
	uint16_t ReperA1;  // Координаты по азимуту реперной точки 1
	uint16_t NTSO2;	// Номер цикла обзора для реперной точки 2
	uint16_t ReperR2;  // Координаты по дальности реперной точки 2
	uint16_t ReperA2;  // Координаты по азимуту реперной точки 2
	uint16_t NTSO3;	// Номер цикла обзора для реперной точки 3
	uint16_t ReperR3;  // Координаты по дальности реперной точки 3
	uint16_t ReperA3;  // Координаты по азимуту реперной точки 3
	uint16_t NTSO4;	// Номер цикла обзора для реперной точки 4
	uint16_t ReperR4;  // Координаты по дальности реперной точки 4
	uint16_t ReperA4;  // Координаты по азимуту реперной точки 4
} PrinyatReperBody; // Итого: 12 * 2 = 24 байта

// [4.2.12] «Принять параметры СДР» (тело сообщения)
typedef struct {
	uint8_t pp_nl;	// Режим работы РСА и номер луча (РР и НЛ)
	uint8_t brl;	  // Маска бланкирования рабочих лучей (БРЛ)
	uint8_t kdec;	 // Коэффициент прореживания (KDEC)
	uint8_t yo;	   // Уровень обработки (УО)
	uint8_t sland;	// Доля площади ячейки, занимаемая сушей (SLand)
	uint8_t sf;	   // Доля площади ячейки, занимаемая НК (SF)
	uint8_t t0;	   // Коэффициент порогового обнаружения суши (t0)
	uint8_t t1;	   // Коэффициент порогового обнаружения НК на море (t1)
	uint8_t q0;	   // Пороговая константа обнаружения активной помехи (Qo)
	uint16_t q;		// Нормализованная константа для сигмы шума (Q)
	uint8_t aru;	  // Режим работы АРУ устройства МПУ 11B521-4 (ARU)
	uint8_t karu;	 // Константа значения кода аттенюации устр-ва МПУ 11B521-4 в режиме внешнего кода АРУ (KARU)
	uint16_t sigmaybm; // Константа номинального среднеквадратичного уровня шума на выходе устройства МПУ 11B521-4 (SIGMAYBM)
	uint8_t kw;	   // Код включения взвешивания (KW)
	uint8_t w_23[23]; // Массив коэффициентов взвешивающего фильтра W[23]
	uint16_t nfft;	// Количество отсчётов БПФ (NFFT)
	uint8_t or_param; // Размер ячейки порогового обнаружителя в отсчётах по дальности (OR)
	uint8_t oa;	   // Размер ячейки порогового обнаружителя в отсчётах по азимуту (ОА)
	uint16_t mrr;	 // Количество отсчетов в опоре по дальности (MRR)
	uint16_t fixp;	// Уровень фиксированного порога (FixP)
} PrinyatParametrySdrBody;

// [4.2.13] «Принять параметры 3ЦО» (тело сообщения) - Таблица 4.31
typedef struct {
	uint16_t Rezerv;			// Резерв (2 байта)
	uint16_t Ncadr;			 // Количество строк дальности в СУБК (2 байта)
	uint8_t  Xnum;			  // Количество строк дальности в СУБК для OP1 (1 байт)
	uint8_t  DNA[8][16];		// Диаграмма направленности антенны для OP (128 байт)
	uint8_t  DNA_INVERS[8][16]; // Инверсная диаграмма для OP (128 байт)
	uint8_t  DNA_OR1[16];	   // Диаграмма направленности для OP1 (16 байт)
	uint8_t  DNA_INVERS_OR1[16];// Инверсная диаграмма для OP1 (16 байт)
	uint16_t Q1;				// Нормализованная константа в OP (2 байта)
	uint16_t Q1_OR1;			// Нормализованная константа в OP1 (2 байта)
	uint8_t  Part;			  // Доля суши для СУБК в OP (1 байт)
	uint8_t  Sea_or_land[16];   // Массив береговой линии для СУБК в OP (16 байт)
} PrinyatParametry3TsoBody; // Итого: 2+2+1+128+128+16+16+2+2+1+16 = 314 байт

// [4.2.14] «Принять REF_AZIMUTH» (тело сообщения) - Таблица 4.33
#define REF_AZIMUTH_ELEMENT_COUNT (512 * 16 * 2) // 16384 элементов
typedef struct {
	uint16_t NTSO;							 // Номер цикла обзора, с которого действуют параметры
	int16_t  ref_azimuth[REF_AZIMUTH_ELEMENT_COUNT]; // Массив азимутальных опор (16384 * 2 = 32768 байт)
												   // Используем int16_t вместо complex_fixed16_t
} PrinyatRefAzimuthBody; // Итого: 2 + 32768 = 32770 байт

// [4.2.15] «Принять параметры ЦДР» (тело сообщения)
typedef struct {
	uint16_t rezerv;	 // Резерв
	uint16_t nin;	   // Количество строк дальности (Nin)
	uint16_t nout;	  // Количество строк амплитудного изображения (Nout)
	uint16_t mrn;	   // Количество отсчётов в непрореженной строке дальности (MRn)
	uint8_t shmr;	   // Максимальный модуль сдвигов отсчётов строк по дальности (ShMR)
	uint8_t nar;		// Количество строк матрицы опор по азимуту (NAR)
	int8_t okm[1024];	// Массив обратной коррекции миграции дальности целей OKM[Nout], assumed max Nout = 1024 for now
	uint8_t hshmr[1024];  // Массив смещений строк по дальности HShMR[Nin], assumed max Nin = 1024 for now
	uint8_t har[54400];  // Матрица опор по азимуту HAR[NAR, Nin], assumed max NAR*Nin = 54400 for now, using uint8_t as placeholder, will need complex fixed16 later
} PrinyatParametryTsdBody;

// [4.2.16] «Навигационные данные» (тело сообщения)
typedef struct {
	uint8_t mnd[256]; // Массив навигационных данных (МНД)
} NavigatsionnyeDannyeBody;

/********************************************************************************/
/*						 ПРОТОТИПЫ ФУНКЦИЙ ИЗ COMMON.C						 */
/********************************************************************************/

// Создание сообщений
Message create_init_channel_message(LogicalAddress uvm_address, LogicalAddress svm_address, uint16_t message_num); // 4.2.1. «Инициализация канала»
Message create_confirm_init_message(LogicalAddress svm_address, uint8_t slp, uint8_t vdr, uint8_t vor1, uint8_t vor2, uint32_t bcb, uint16_t message_num); // 4.2.2. «Подтверждение инициализации канала»
Message create_provesti_kontrol_message(LogicalAddress svm_address, uint8_t tk, uint16_t message_num); // 4.2.3. «Провести контроль»
Message create_podtverzhdenie_kontrolya_message(LogicalAddress svm_address, uint8_t tk, uint32_t bcb, uint16_t message_num); // 4.2.4. «Подтверждение контроля»
Message create_vydat_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t vpk, uint16_t message_num); // 4.2.5. «Выдать результаты контроля»
Message create_rezultaty_kontrolya_message(LogicalAddress svm_address, uint8_t rsk, uint16_t bck, uint32_t bcb, uint16_t message_num); // 4.2.6. «Результаты контроля»
Message create_vydat_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.7. «Выдать состояние линии»
Message create_sostoyanie_linii_message(LogicalAddress svm_address, uint16_t kla, uint32_t sla, uint16_t ksa, uint32_t bcb, uint16_t message_num); // 4.2.8. «Состояние линии»
Message create_prinyat_parametry_so_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.9. «Принять параметры СО»
Message create_prinyat_time_ref_range_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.10. «Принять TIME_REF_RANGE»
Message create_prinyat_reper_message(LogicalAddress svm_address, uint16_t message_num);		  // 4.2.11. «Принять Reper»
Message create_prinyat_parametry_sdr_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.12. «Принять параметры СДР»
Message create_prinyat_parametry_3tso_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.13. «Принять параметры 3ЦО»
Message create_prinyat_ref_azimuth_message(LogicalAddress svm_address, uint16_t message_num);   // 4.2.14. «Принять REF_AZIMUTH»
Message create_prinyat_parametry_tsd_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.15. «Принять параметры ЦДР»
Message create_navigatsionnye_dannye_message(LogicalAddress svm_address, uint16_t message_num); // 4.2.16. «Навигационные данные»

uint16_t get_full_message_number(const MessageHeader *header); // Получить полный номер сообщения (на основе флагов и номера сообщения)
void message_to_network_byte_order(Message *message); // Преобразовать сообщение в сетевой порядок байтов (Network Byte Order)
void message_to_host_byte_order(Message *message); // Преобразовать сообщение в порядок байтов хоста (Host Byte Order)
int send_message(int socketFD, Message *message); // Отправить сообщение через сокет

#endif // COMMON_H