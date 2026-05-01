#include <stdio.h>
#include "arinc429.h"

/* Вспомогательная функция печати ARINC-слова для отладки */
void print_arinc_word(const char* title, arinc_word_t* w) {
    printf("[%s] Raw: 0x%08X | Label: 0x%02X | SDI: %d | SSM: %d | Valid: %d\n",
           title, w->raw_word, w->label, w->sdi, w->ssm, w->is_valid);
}

int main(void)
{
    /* Инициализация основного канала ARINC */
    arinc_ctx_t bus;
    arinc_init(&bus, true);

    /* Дескрипторы для типовых параметров (диапазоны из ТЗ на ВСУ) */
    arinc_bnr_desc_t desc_n1  = { .range_min = 0.0f, .range_max = 120.0f, .data_bits = 19 };
    arinc_bnr_desc_t desc_egt = { .range_min = -100.0f, .range_max = 900.0f, .data_bits = 19 };

    /* =========================================================================
     * СЦЕНАРИЙ 1: Циклическая передача NCD (согласно конспекту)
     * ARINC крутит слова постоянно. Если данных нет, передаётся SSM=01.
     * ========================================================================= */
    printf("=== 1. Цикл NCD (Нет вычисленных данных) ===\n");
    arinc_set_status(&bus, false, false); /* Нет отказа, не тест */
    uint8_t ssm = arinc_get_current_ssm(&bus); /* Вернёт 01 */
    arinc_word_t w_ncd = { .label=0x84, .sdi=SDI_UNIVERSAL, .ssm=ssm };
    w_ncd.raw_word = arinc_pack_bnr(w_ncd.label, 0.0f, w_ncd.ssm, w_ncd.sdi, &desc_n1);
    arinc_push_tx(&bus, &w_ncd);
    print_arinc_word("N1 NCD", &bus.tx_buffer[0]);

    /* =========================================================================
     * СЦЕНАРИЙ 2: Нормальная работа (SSM=11)
     * ========================================================================= */
    printf("\n=== 2. Нормальная телеметрия (SSM=11) ===\n");
    arinc_word_t w_normal = { .label=0x84, .sdi=SDI_UNIVERSAL, .ssm=SSM_NORMAL_OPERATION };
    w_normal.raw_word = arinc_pack_bnr(w_normal.label, 85.5f, w_normal.ssm, w_normal.sdi, &desc_n1);
    arinc_push_tx(&bus, &w_normal);
    print_arinc_word("N1 Normal", &bus.tx_buffer[1]);

    /* =========================================================================
     * СЦЕНАРИЙ 3: Отказ системы (SSM=00)
     * При fault=true все последующие слова будут иметь SSM=00.
     * Поле Data игнорируется приёмником.
     * ========================================================================= */
    printf("\n=== 3. Обработка отказа (SSM=00) ===\n");
    arinc_set_status(&bus, true, false); /* Активируем отказ */
    arinc_word_t w_fail = { .label=0x86, .sdi=SDI_UNIVERSAL, .ssm=SSM_FAILURE_WARNING };
    w_fail.raw_word = arinc_pack_bnr(w_fail.label, 999.0f, w_fail.ssm, w_fail.sdi, &desc_egt);
    arinc_push_tx(&bus, &w_fail);
    print_arinc_word("EGT Fail", &bus.tx_buffer[2]);

    /* =========================================================================
     * СЦЕНАРИЙ 4: Функциональный тест и сброс отказов
     * Reset на ПУ отсутствует. Отказы сбрасываются ТОЛЬКО через TEST.
     * ========================================================================= */
    printf("\n=== 4. Тестовый режим и сброс (SSM=10) ===\n");
    arinc_set_status(&bus, false, true); /* Вход в тест (сброс отказа) */
    arinc_word_t w_test = { .label=0x70, .sdi=SDI_UNIVERSAL, .ssm=SSM_FUNCTIONAL_TEST };
    w_test.raw_word = arinc_pack_discrete(w_test.label, 0x1, w_test.ssm, w_test.sdi);
    arinc_push_tx(&bus, &w_test);
    print_arinc_word("TEST CMD", &bus.tx_buffer[3]);

    /* =========================================================================
     * СЦЕНАРИЙ 5: Расчёт времени цикла передачи
     * 100 кбит/с → 10 мкс/бит. Слово = 36 бит (32+4). 5 слов = 1.8 мс.
     * ========================================================================= */
    printf("\n=== 5. Тайминги шины ARINC 429 ===\n");
    uint8_t words_count = 5; /* N1, EGT, P3, T3, STATUS */
    uint32_t cycle_us = arinc_calc_cycle_time_us(words_count);
    printf("Цикл передачи %d слов: %lu мкс (%.2f мс)\n", words_count, cycle_us, cycle_us/1000.0);
    printf("Частота обновления параметров: %.1f Гц\n", 1000000.0 / cycle_us);

    /* =========================================================================
     * СЦЕНАРИЙ 6: Приём, декодирование и проверка чётности
     * ========================================================================= */
    printf("\n=== 6. Приём и распаковка слов ===\n");
    arinc_word_t* rx = arinc_pop_tx(&bus);
    while (rx) {
        arinc_word_t decoded = arinc_decode_word(rx->raw_word);
        printf("Приём: Label=0x%02X SSM=%d Valid=%d", decoded.label, decoded.ssm, decoded.is_valid);
        
        if (decoded.is_valid && decoded.label == 0x84) {
            float val = arinc_unpack_bnr(decoded.raw_word, &desc_n1);
            printf(" | unpacked: %.2f %%\n", val);
        } else {
            printf(" (данные игнорируются по SSM)\n");
        }
        rx = arinc_pop_tx(&bus);
    }

    return 0;
}
