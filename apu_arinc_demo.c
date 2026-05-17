#include <stdio.h>
#include <math.h>
#include "arinc429.h"
#include "apu_arinc_demo.h"

/* КРАСИВЫЙ ВЫВОД 32-БИТНОГО СЛОВА ARINC, показывает разбивку по полям: Parity | SSM | Data | SDI | Label */
static void print_arinc_word(uint32_t word, const char* name)
{
    printf("  [%-10s] 0x%08X | ", name, word);
    
    /* Вывод битов с разделителями по границам полей ARINC */
    for (int i = 31; i >= 0; i--) {
        printf("%d", (word >> i) & 1);
        /* Разделители: бит 31 (Parity), биты 29-30 (SSM), биты 9-10 (SDI) */
        if (i == 31 || i == 29 || i == 9) {
            printf(" | ");
        }
    }
    printf(" (P|SSM|DATA|SDI|LBL)\n");
}

/*  УПАКОВКА BCD ДЛЯ ОБОРОТОВ (0-120% -> 3 десятичные цифры). Каждая цифра кодируется 4 битами в поле Data */
static uint32_t pack_bcd_rpm(uint8_t label, uint8_t value, uint8_t ssm, uint8_t sdi)
{
    uint32_t word = 0;
    
    /* Label (биты 1-8) */
    word |= (label & 0xFFu);
    
    /* SDI (биты 9-10) */
    word |= ((sdi & 0x3u) << 8);
    
    /* BCD: каждая цифра занимает 4 бита. Пример: 120 -> 0x120 */
    uint32_t bcd = ((value / 100) << 8) | (((value / 10) % 10) << 4) | (value % 10);
    word |= (bcd << 10);               /* Сдвиг в поле Data (биты 11-29) */
    
    /* SSM (биты 30-31) */
    word |= ((ssm & 0x3u) << 29);
    
    /* Нечётная чётность (бит 32) */
    uint8_t p = 0;
    for (int i = 0; i < 31; i++) {
        if ((word >> i) & 1u) p++;
    }
    if (p % 2 == 0) word |= (1u << 31);
    
    return word;
}

/* РАСПАКОВКА BCD. Извлекает 3 десятичные цифры из поля Data*/
static uint8_t unpack_bcd_rpm(uint32_t word)
{
    uint32_t data = (word >> 10) & 0x7FFFFu;
    return ((data >> 8) & 0xF) * 100 + ((data >> 4) & 0xF) * 10 + (data & 0xF);
}

/* ГЛАВНАЯ ФУНКЦИЯ: ИСХОДНЫЕ -> КОДИРОВАНИЕ -> ДЕКОДИРОВАНИЕ. Вызывается из start_scenario() для демонстрации ARINC 429 */
void arinc_process_rsc_response(const RCS_response_t* raw)
{
    printf("\n========================================\n");
    printf("  ARINC 429: Обработка RCS_response\n");
    printf("========================================\n");

    /* 1. ПОЛУЧЕНИЕ ДАННЫХ ПО АНАЛОГОВЫМ КАНАЛАМ (эмуляция АЦП) */
    float egt_adc = raw->egt_celsius;
    float rpm_adc = raw->rotor_n_percent;
    printf("[Аналоговые каналы] EGT: %6.2f °C | RPM: %5.2f %%\n", egt_adc, rpm_adc);

    /* Дескриптор BNR для EGT (-100..900°C, 19 бит) */
    arinc_bnr_desc_t desc_egt = {-100.0f, 900.0f, 19};
    uint8_t ssm = SSM_NORMAL_OPERATION;
    uint8_t sdi = SDI_UNIVERSAL;

    /* 2. КОДИРОВАНИЕ В ARINC 429 */
    
    /* BNR: Температура выхлопа (Label 206₈ = 0x86) */
    uint32_t word_egt = arinc_pack_bnr(0x86, egt_adc, ssm, sdi, &desc_egt);
    
    /* BCD: Обороты ротора (Label 204₈ = 0x84) */
    uint32_t word_rpm = pack_bcd_rpm(0x84, (uint8_t)roundf(rpm_adc), ssm, sdi);
    
    /* DW: 7 дискретных признаков в одном слове (Label 300₈ = 0xC0) */
    uint32_t dw = 0;
    dw |= (raw->discrete.gen_on          ? 1u : 0u) << 0;  /* Бит 0 */
    dw |= (raw->discrete.bleed_open      ? 1u : 0u) << 1;  /* Бит 1 */
    dw |= (raw->discrete.fault_flag      ? 1u : 0u) << 2;  /* Бит 2 */
    dw |= (raw->discrete.ready_to_start  ? 1u : 0u) << 3;  /* Бит 3 */
    dw |= (raw->discrete.running         ? 1u : 0u) << 4;  /* Бит 4 */
    dw |= (raw->discrete.test_mode       ? 1u : 0u) << 5;  /* Бит 5 */
    dw |= (raw->discrete.emergency_stop  ? 1u : 0u) << 6;  /* Бит 6 */
    uint32_t word_dw = arinc_pack_discrete(0xC0, dw, ssm, sdi);

    printf("\n[Кодирование ARINC 429]\n");
    print_arinc_word(word_egt, "EGT BNR");
    print_arinc_word(word_rpm, "RPM BCD");
    print_arinc_word(word_dw,  "STAT DW");

    /* 3. ДЕКОДИРОВАНИЕ (имитация приёма на стороне СЭС/СДУ) */
    printf("\n[Декодирование на приёмнике]\n");
    
    float dec_egt   = arinc_unpack_bnr(word_egt, &desc_egt);
    uint8_t dec_rpm = unpack_bcd_rpm(word_rpm);
    uint32_t dec_dw = (word_dw >> 10) & 0x7FFFFu;

    printf("  EGT (BNR)   : Исход %6.2f -> Декод %6.2f °C\n", egt_adc, dec_egt);
    printf("  RPM (BCD)   : Исход %3d %% -> Декод %3d %%\n", 
           (uint8_t)roundf(rpm_adc), dec_rpm);
    printf("  STAT (DW)   : Флаги 0x%03X -> G:%d B:%d F:%d R:%d W:%d T:%d E:%d\n",
           dec_dw, 
           (dec_dw>>0)&1, (dec_dw>>1)&1, (dec_dw>>2)&1,
           (dec_dw>>3)&1, (dec_dw>>4)&1, (dec_dw>>5)&1, (dec_dw>>6)&1);

    /* 4. ПРОВЕРКА КРУГОВОГО ПРОХОДА */
    bool ok = (fabsf(egt_adc - dec_egt) < 0.05f) && 
              (dec_rpm == (uint8_t)roundf(rpm_adc)) && 
              (dw == dec_dw);
    
    printf("\n[Статус] %s\n", ok ? "PASSED" : "FAILED");
    printf("========================================\n");
}
