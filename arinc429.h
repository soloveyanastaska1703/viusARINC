#ifndef ARINC429_H
#define ARINC429_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 * СПЕЦИФИКАЦИЯ ARINC 429 (согласно конспекту ВИУС)
 * ============================================================================ */
#define ARINC_WORD_BITS       32u                 /* Длина слова: 32 бита */
#define ARINC_SPEED_KBIT      100u                /* Скорость передачи: 100 кбит/с (низкоскоростной режим) */
#define ARINC_BIT_TIME_US     (1000u / ARINC_SPEED_KBIT) /* 10 мкс на бит */
#define ARINC_GAP_BITS        4u                  /* Минимальный межсловный интервал */
/* Время цикла: 1 слово = 32 бит данных + 4 бита gap = 36 бит. При 100 кбит/с → 360 мкс */
#define ARINC_CYCLE_TIME_US(N) ((N) * (ARINC_WORD_BITS + ARINC_GAP_BITS) * ARINC_BIT_TIME_US)

/* Маски полей слова (в C нумерация с 0, в ARINC с 1) */
#define ARINC_MASK_LABEL      0x000000FFu         /* Биты 1-8   (0-7)   : Метка (Label) */
#define ARINC_MASK_SDI        0x00000300u         /* Биты 9-10  (8-9)   : Идентификатор источника/приёмника */
#define ARINC_MASK_DATA       0x1FFFC000u         /* Биты 11-29 (10-28) : Поле данных */
#define ARINC_MASK_SSM        0x60000000u         /* Биты 30-31 (29-30) : Матрица статуса/знака */
#define ARINC_MASK_PARITY     0x80000000u         /* Бит 32     (31)    : Бит нечётной чётности */

/* Коды SSM (Sign/Status Matrix) - таблица из конспекта */
#define SSM_FAILURE_WARNING     0x0u  /* 00 — Отказ/Предупреждение (Failure Warning) */
#define SSM_NO_COMPUTED_DATA    0x1u  /* 01 — Данные не вычислены (No Computed Data) */
#define SSM_FUNCTIONAL_TEST     0x2u  /* 10 — Функциональный тест (Functional Test) */
#define SSM_NORMAL_OPERATION    0x3u  /* 11 — Нормальная работа (Normal Operation) */

/* Коды SDI (Source/Destination Identifier) */
#define SDI_UNIVERSAL           0x0u  /* 00 — Универсальный / все приёмники */
#define SDI_LEFT_CHANNEL        0x1u  /* 01 — Левый канал / 1-й порт (КВС) */
#define SDI_RIGHT_CHANNEL       0x2u  /* 10 — Правый канал / 2-й порт (2П) */
#define SDI_RESERVED            0x3u  /* 11 — Зарезервировано */

/* ============================================================================
 * СТРУКТУРЫ ДАННЫХ
 * ============================================================================ */
/* Структура слова с метаданными для удобства работы */
typedef struct {
    uint32_t raw_word;   /* Сырое 32-битное слово для отправки/приёма */
    uint8_t  label;      /* Метка параметра (в ARINC задаётся в восьмеричной системе, в C передаём hex) */
    uint8_t  sdi;        /* Идентификатор канала */
    uint8_t  ssm;        /* Статус данных */
    bool     is_valid;   /* true если SSM == NORMAL или TEST (данные можно использовать) */
} arinc_word_t;

/* Дескриптор масштабирования для типа данных BNR (Binary) */
typedef struct {
    float  range_min;    /* Минимальное физическое значение диапазона */
    float  range_max;    /* Максимальное физическое значение диапазона */
    uint8_t data_bits;   /* Количество бит в поле Data (обычно 19 для знаковых величин) */
} arinc_bnr_desc_t;

/* Контекст передатчика/приёмника шины */
typedef struct {
    arinc_word_t tx_buffer[16]; /* Кольцевой буфер на передачу */
    uint8_t tx_head, tx_tail, tx_count;
    arinc_word_t rx_buffer[16]; /* Буфер принятых слов */
    uint8_t rx_head;
    bool is_primary;            /* Основной или резервный канал ARINC */
    bool fault_active;          /* Глобальный флаг отказа системы */
    bool test_mode;             /* Режим функционального теста */
} arinc_ctx_t;

/* ============================================================================
 * ПРОТОТИПЫ ФУНКЦИЙ (ТОЛЬКО ARINC)
 * ============================================================================ */
void arinc_init(arinc_ctx_t* ctx, bool is_primary);
uint8_t arinc_calc_parity(uint32_t word);

/* Упаковка данных */
uint32_t arinc_pack_bnr(uint8_t label, float value, uint8_t ssm, uint8_t sdi, const arinc_bnr_desc_t* desc);
uint32_t arinc_pack_bcd(uint8_t label, uint32_t bcd_val, uint8_t ssm, uint8_t sdi);
uint32_t arinc_pack_discrete(uint8_t label, uint32_t flags, uint8_t ssm, uint8_t sdi);

/* Распаковка и декодирование */
arinc_word_t arinc_decode_word(uint32_t raw);
float arinc_unpack_bnr(uint32_t raw, const arinc_bnr_desc_t* desc);

/* Управление очередью передачи */
bool arinc_push_tx(arinc_ctx_t* ctx, const arinc_word_t* word);
arinc_word_t* arinc_pop_tx(arinc_ctx_t* ctx);

/* Тайминг и статусы */
uint32_t arinc_calc_cycle_time_us(uint8_t num_words);
void arinc_set_status(arinc_ctx_t* ctx, bool fault, bool test);
uint8_t arinc_get_current_ssm(arinc_ctx_t* ctx);

#endif /* ARINC429_H */
