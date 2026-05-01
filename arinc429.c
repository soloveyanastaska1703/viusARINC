#include "arinc429.h"
#include <string.h>

/* Инициализация контекста шины ARINC */
void arinc_init(arinc_ctx_t* ctx, bool is_primary)
{
    memset(ctx, 0, sizeof(arinc_ctx_t));
    ctx->is_primary = is_primary;
    /* По умолчанию шина в состоянии NCD (01), пока не придут валидные данные или команда теста */
    ctx->fault_active = false;
    ctx->test_mode = false;
}

/* Расчёт бита нечётной чётности (Odd Parity) по битам 1-31 (в C: 0..30) */
uint8_t arinc_calc_parity(uint32_t word)
{
    uint8_t ones = 0;
    for (int i = 0; i < 31; i++) {
        if ((word >> i) & 1u) ones++;
    }
    /* Если число единиц чётное, бит 32 ставим в 1, чтобы общая сумма стала нечётной */
    return (ones % 2 == 0) ? 1 : 0;
}

/* Упаковка данных типа BNR (Binary) согласно формуле из конспекта:
 * LSB = диапазон / 2^(разрядность-1)
 * Data = Значение / LSB */
uint32_t arinc_pack_bnr(uint8_t label, float value, uint8_t ssm, uint8_t sdi, const arinc_bnr_desc_t* desc)
{
    uint32_t word = 0;
    word |= (label & ARINC_MASK_LABEL);
    word |= ((sdi & 0x3u) << 8);
    word |= ((ssm & 0x3u) << 29);

    /* Поле Data заполняется ТОЛЬКО при NORMAL (11) или TEST (10) */
    if (ssm == SSM_NORMAL_OPERATION || ssm == SSM_FUNCTIONAL_TEST) {
        float range = desc->range_max - desc->range_min;
        /* Масштабирование в 19-битное знаковое целое (two's complement) */
        int32_t raw = (int32_t)((value - desc->range_min) * (float)(1u << (desc->data_bits - 1)) / range);
        /* Ограничение 19 битами (0x7FFFF) */
        raw = (raw >  0x3FFFF) ?  0x3FFFF : raw;
        raw = (raw < -0x40000) ? -0x40000 : raw;
        word |= ((uint32_t)(raw & 0x7FFFF) << 10);
    }

    /* Установка бита чётности (бит 32) */
    if (arinc_calc_parity(word)) word |= ARINC_MASK_PARITY;
    return word;
}

/* Упаковка данных типа BCD (Binary Coded Decimal) для человеко-читаемых цифр */
uint32_t arinc_pack_bcd(uint8_t label, uint32_t bcd_val, uint8_t ssm, uint8_t sdi)
{
    uint32_t word = 0;
    word |= (label & ARINC_MASK_LABEL);
    word |= ((sdi & 0x3u) << 8);
    word |= ((ssm & 0x3u) << 29);
    /* Каждая десятичная цифра кодируется 4 битами, размещаем в поле Data */
    word |= ((bcd_val & 0x1FFFFu) << 10);
    if (arinc_calc_parity(word)) word |= ARINC_MASK_PARITY;
    return word;
}

/* Упаковка дискретных данных (Discrete Word) - 1 бит = 1 флаг */
uint32_t arinc_pack_discrete(uint8_t label, uint32_t flags, uint8_t ssm, uint8_t sdi)
{
    uint32_t word = 0;
    word |= (label & ARINC_MASK_LABEL);
    word |= ((sdi & 0x3u) << 8);
    word |= ((flags & 0x7FFFFu) << 10); /* Дискретные флаги в битах 11-29 */
    word |= ((ssm & 0x3u) << 29);
    if (arinc_calc_parity(word)) word |= ARINC_MASK_PARITY;
    return word;
}

/* Декодирование сырого слова в структуру с проверкой статуса */
arinc_word_t arinc_decode_word(uint32_t raw)
{
    arinc_word_t decoded;
    decoded.raw_word = raw;
    decoded.label    = (raw >> 0)  & 0xFF;
    decoded.sdi      = (raw >> 8)  & 0x3;
    decoded.ssm      = (raw >> 29) & 0x3;
    /* Данные считаются валидными только при NORMAL или TEST (согласно логике приёмника) */
    decoded.is_valid = (decoded.ssm == SSM_NORMAL_OPERATION || decoded.ssm == SSM_FUNCTIONAL_TEST);
    return decoded;
}

/* Распаковка BNR данных обратно во float */
float arinc_unpack_bnr(uint32_t raw, const arinc_bnr_desc_t* desc)
{
    uint32_t data = (raw & ARINC_MASK_DATA) >> 10;
    /* Обработка знакового бита (19-й бит поля Data) для two's complement */
    if (data & 0x40000u) {
        data |= 0xFFF80000u; /* Знаковое расширение до 32 бит */
    }
    int32_t signed_data = (int32_t)data;
    float range = desc->range_max - desc->range_min;
    return desc->range_min + (float)signed_data * range / (float)(1u << (desc->data_bits - 1));
}

/* Добавление слова в очередь передачи */
bool arinc_push_tx(arinc_ctx_t* ctx, const arinc_word_t* word)
{
    if (ctx->tx_count >= 16) return false;
    ctx->tx_buffer[ctx->tx_head] = *word;
    ctx->tx_head = (ctx->tx_head + 1) % 16;
    ctx->tx_count++;
    return true;
}

/* Извлечение следующего слова для отправки (циклическая передача) */
arinc_word_t* arinc_pop_tx(arinc_ctx_t* ctx)
{
    if (ctx->tx_count == 0) return NULL;
    arinc_word_t* word = &ctx->tx_buffer[ctx->tx_tail];
    ctx->tx_tail = (ctx->tx_tail + 1) % 16;
    ctx->tx_count--;
    return word;
}

/* Расчёт времени цикла передачи */
uint32_t arinc_calc_cycle_time_us(uint8_t num_words)
{
    /* 1 слово = 32 бита данных + 4 бита межсловного интервала = 36 бит */
    /* При 100 кбит/с: 1 бит = 10 мкс → 1 слово = 360 мкс */
    return ARINC_CYCLE_TIME_US(num_words);
}

/* Управление статусом системы (отказ / тест) */
void arinc_set_status(arinc_ctx_t* ctx, bool fault, bool test)
{
    ctx->fault_active = fault;
    ctx->test_mode = test;
}

/* Автоматический выбор SSM согласно логике из конспекта:
 *  - Есть отказ → 00 (Failure Warning)
 *  - Режим теста → 10 (Functional Test)
 *  - По умолчанию → 01 (No Computed Data), крутится в цикле пока нет данных/теста */
uint8_t arinc_get_current_ssm(arinc_ctx_t* ctx)
{
    if (ctx->fault_active) return SSM_FAILURE_WARNING;
    if (ctx->test_mode)    return SSM_FUNCTIONAL_TEST;
    return SSM_NO_COMPUTED_DATA;
}
