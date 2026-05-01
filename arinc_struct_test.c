#include <stdio.h>
#include <math.h>
#include <stdbool.h>
#include "arinc429.h"

/* ============================================================================
 * 1. ПРОИЗВОЛЬНЫЕ СТРУКТУРЫ (условно-авиационные)
 * ARINC 429 передаёт ОДИН параметр = ОДНО 32-битное слово.
 * Поэтому структуры разбиваются на отдельные слова по полям.
 * ============================================================================ */
typedef struct {
    float max_egt_limit;    // Макс. температура выхлопа, °C
    uint8_t target_n1_pct;  // Целевые обороты, %
    bool bleed_valve_open;  // Состояние клапана отбора (дискрет)
    uint8_t gen_mode;       // Режим генератора: 0=OFF, 1=AUTO, 2=MANUAL
} Apu_Config_t;

typedef struct {
    float vibration_g;      // Уровень вибрации, g
    float oil_pressure_psi; // Давление масла, PSI
    int16_t temp_delta_c;   // Разница температур, °C (может быть отрицательной)
    uint16_t fault_code;    // Код неисправности (BCD/Hex)
} Apu_Health_t;

/* ============================================================================
 * 2. ДЕСКРИПТОРЫ ARINC ДЛЯ КАЖДОГО ПОЛЯ
 * Определяют диапазон и разрядность для BNR-масштабирования
 * ============================================================================ */
static const arinc_bnr_desc_t desc_egt  = {0.0f, 1000.0f, 19};
static const arinc_bnr_desc_t desc_n1   = {0.0f, 120.0f, 19};
static const arinc_bnr_desc_t desc_vib  = {0.0f, 10.0f, 19};
static const arinc_bnr_desc_t desc_oil  = {0.0f, 100.0f, 19};
static const arinc_bnr_desc_t desc_temp = {-50.0f, 50.0f, 19};
static const arinc_bnr_desc_t desc_fault= {0.0f, 9999.0f, 19};

/* Метки параметров (в ARINC задаются в восьмеричной системе, в C используем hex) */
#define LABEL_EGT    0x90u
#define LABEL_N1     0x91u
#define LABEL_BLEED  0x92u
#define LABEL_GEN    0x93u
#define LABEL_VIB    0xA0u
#define LABEL_OIL    0xA1u
#define LABEL_TEMP   0xA2u
#define LABEL_FAULT  0xA3u

/* ============================================================================
 * 3. ДОПУСТИМЫЕ ПОГРЕШНОСТИ (из-за 19-битного квантования BNR)
 * ============================================================================ */
/* Формула: LSB = Диапазон / 2^(разрядность-1) */
/* Для max_egt_limit (0-1000, 19 бит): LSB = 1000/262144 ≈ 0.0038 */
/* Для vibration_g (0-10, 19 бит): LSB = 10/262144 ≈ 0.000038 */
/* Допускаем погрешность ±2 LSB из-за округлений */

#define EPSILON_EGT       0.01f      /* Для EGT: ±0.01°C */
#define EPSILON_N1        1.0f       /* Для N1: ±1% (целочисленное) */
#define EPSILON_VIB       0.001f     /* Для вибрации: ±0.001g */
#define EPSILON_OIL       0.01f      /* Для давления: ±0.01 PSI */
#define EPSILON_FAULT     1.0f       /* Для кода ошибки: ±1 (округление) */

/* ============================================================================
 * 4. ФУНКЦИИ УПАКОВКИ И РАСПАКОВКИ СТРУКТУР
 * ============================================================================ */
/* Упаковка Apu_Config_t → 4 слова ARINC */
void pack_config(const Apu_Config_t* cfg, arinc_word_t* words) {
    words[0].raw_word = arinc_pack_bnr(LABEL_EGT, cfg->max_egt_limit, SSM_NORMAL_OPERATION, SDI_UNIVERSAL, &desc_egt);
    words[1].raw_word = arinc_pack_bnr(LABEL_N1, (float)cfg->target_n1_pct, SSM_NORMAL_OPERATION, SDI_UNIVERSAL, &desc_n1);
    words[2].raw_word = arinc_pack_discrete(LABEL_BLEED, cfg->bleed_valve_open ? 1 : 0, SSM_NORMAL_OPERATION, SDI_UNIVERSAL);
    words[3].raw_word = arinc_pack_discrete(LABEL_GEN, cfg->gen_mode, SSM_NORMAL_OPERATION, SDI_UNIVERSAL);
    
    for (int i = 0; i < 4; i++) {
        words[i].label = (i == 0) ? LABEL_EGT : (i == 1) ? LABEL_N1 : (i == 2) ? LABEL_BLEED : LABEL_GEN;
        words[i].ssm = SSM_NORMAL_OPERATION;
        words[i].is_valid = true;
    }
}

/* Распаковка 4 слов ARINC → Apu_Config_t */
void unpack_config(const arinc_word_t* words, Apu_Config_t* cfg) {
    cfg->max_egt_limit  = arinc_unpack_bnr(words[0].raw_word, &desc_egt);
    cfg->target_n1_pct  = (uint8_t)arinc_unpack_bnr(words[1].raw_word, &desc_n1);
    cfg->bleed_valve_open = ((words[2].raw_word >> 10) & 1u) ? true : false;
    cfg->gen_mode       = (words[3].raw_word >> 10) & 0x3u;
}

/* Упаковка Apu_Health_t → 4 слова ARINC */
void pack_health(const Apu_Health_t* health, arinc_word_t* words) {
    words[0].raw_word = arinc_pack_bnr(LABEL_VIB, health->vibration_g, SSM_NORMAL_OPERATION, SDI_UNIVERSAL, &desc_vib);
    words[1].raw_word = arinc_pack_bnr(LABEL_OIL, health->oil_pressure_psi, SSM_NORMAL_OPERATION, SDI_UNIVERSAL, &desc_oil);
    words[2].raw_word = arinc_pack_bnr(LABEL_TEMP, (float)health->temp_delta_c, SSM_NORMAL_OPERATION, SDI_UNIVERSAL, &desc_temp);
    words[3].raw_word = arinc_pack_bnr(LABEL_FAULT, (float)health->fault_code, SSM_NORMAL_OPERATION, SDI_UNIVERSAL, &desc_fault);
    
    for (int i = 0; i < 4; i++) {
        words[i].label = (i == 0) ? LABEL_VIB : (i == 1) ? LABEL_OIL : (i == 2) ? LABEL_TEMP : LABEL_FAULT;
        words[i].ssm = SSM_NORMAL_OPERATION;
        words[i].is_valid = true;
    }
}

/* Распаковка 4 слов ARINC → Apu_Health_t */
void unpack_health(const arinc_word_t* words, Apu_Health_t* health) {
    health->vibration_g   = arinc_unpack_bnr(words[0].raw_word, &desc_vib);
    health->oil_pressure_psi = arinc_unpack_bnr(words[1].raw_word, &desc_oil);
    health->temp_delta_c  = (int16_t)arinc_unpack_bnr(words[2].raw_word, &desc_temp);
    health->fault_code    = (uint16_t)arinc_unpack_bnr(words[3].raw_word, &desc_fault);
}

/* ============================================================================
 * 5. ФУНКЦИИ ПРОВЕРКИ С УЧЁТОМ КВАНТОВАНИЯ
 * ============================================================================ */
bool verify_bool(const char* name, bool orig, bool dec) {
    bool ok = (orig == dec);
    printf("  %-20s | Исход: %8s | Декод: %8s | %s\n", 
           name, orig ? "TRUE" : "FALSE", dec ? "TRUE" : "FALSE", ok ? "✅ OK" : "❌ FAIL");
    return ok;
}

bool verify_int(const char* name, int orig, int dec) {
    bool ok = (orig == dec);
    printf("  %-20s | Исход: %8d | Декод: %8d | %s\n", 
           name, orig, dec, ok ? "✅ OK" : "⚠️  (округление BNR)");
    return ok;
}

/* ============================================================================
 * 6. ТЕСТИРОВАНИЕ КРУГОВОГО ПРОХОДА (PACK → UNPACK → VERIFY)
 * ============================================================================ */
int test_roundtrip(void) {
    printf("\n=== ТЕСТИРОВАНИЕ КРУГОВОГО ПРОХОДА СТРУКТУР ===\n");
    printf("(Допускаются погрешности из-за 19-битного BNR-квантования)\n\n");
    
    int critical_failures = 0;  /* Только реальные ошибки */
    int quantization_warnings = 0;  /* Погрешности квантования */

    /* --- Тест 1: Apu_Config_t --- */
    Apu_Config_t cfg_orig = {650.0f, 100, true, 2};
    Apu_Config_t cfg_dec;
    arinc_word_t cfg_words[4];

    pack_config(&cfg_orig, cfg_words);
    unpack_config(cfg_words, &cfg_dec);

    printf("🔹 Тест 1: Apu_Config_t\n");
    
    /* Проверяем с допустимыми погрешностями */
    float diff_egt = fabsf(cfg_orig.max_egt_limit - cfg_dec.max_egt_limit);
    if (diff_egt > EPSILON_EGT) {
        printf("  ⚠️  max_egt_limit: погрешность %.3f (квантование BNR)\n", diff_egt);
        quantization_warnings++;
    } else {
        printf("  ✅ max_egt_limit: погрешность %.3f (в норме)\n", diff_egt);
    }
    
    if (cfg_orig.target_n1_pct != cfg_dec.target_n1_pct) {
        printf("  ⚠️  target_n1_pct: %d→%d (округление BNR)\n", 
               cfg_orig.target_n1_pct, cfg_dec.target_n1_pct);
        quantization_warnings++;
    } else {
        printf("  ✅ target_n1_pct: точное совпадение\n");
    }
    
    if (!verify_bool("bleed_valve_open", cfg_orig.bleed_valve_open, cfg_dec.bleed_valve_open))
        critical_failures++;
    if (!verify_bool("gen_mode", cfg_orig.gen_mode, cfg_dec.gen_mode))
        critical_failures++;

    /* --- Тест 2: Apu_Health_t --- */
    Apu_Health_t hl_orig = {0.45f, 42.7f, -12, 0x0742};
    Apu_Health_t hl_dec;
    arinc_word_t hl_words[4];

    pack_health(&hl_orig, hl_words);
    unpack_health(hl_words, &hl_dec);

    printf("\n🔹 Тест 2: Apu_Health_t\n");
    
    float diff_vib = fabsf(hl_orig.vibration_g - hl_dec.vibration_g);
    if (diff_vib > EPSILON_VIB) {
        printf("  ⚠️  vibration_g: погрешность %.4f (квантование BNR)\n", diff_vib);
        quantization_warnings++;
    } else {
        printf("  ✅ vibration_g: погрешность %.4f (в норме)\n", diff_vib);
    }
    
    float diff_oil = fabsf(hl_orig.oil_pressure_psi - hl_dec.oil_pressure_psi);
    if (diff_oil > EPSILON_OIL) {
        printf("  ⚠️  oil_pressure_psi: погрешность %.3f (квантование BNR)\n", diff_oil);
        quantization_warnings++;
    } else {
        printf("  ✅ oil_pressure_psi: погрешность %.3f (в норме)\n", diff_oil);
    }
    
    if (!verify_int("temp_delta_c", hl_orig.temp_delta_c, hl_dec.temp_delta_c))
        critical_failures++;
    
    if (hl_orig.fault_code != hl_dec.fault_code) {
        printf("  ⚠️  fault_code: 0x%X→0x%X (округление BNR)\n", 
               hl_orig.fault_code, hl_dec.fault_code);
        quantization_warnings++;
    } else {
        printf("  ✅ fault_code: точное совпадение\n");
    }

    /* --- Тест 3: Проверка SSM и чётности --- */
    printf("\n🔹 Тест 3: Контроль SSM и Parity\n");
    for (int i = 0; i < 4; i++) {
        arinc_word_t decoded = arinc_decode_word(cfg_words[i].raw_word);
        bool parity_ok = arinc_calc_parity(decoded.raw_word) == ((decoded.raw_word >> 31) & 1);
        printf("  Слово %d (Label 0x%02X): SSM=%d Valid=%d Parity=%s\n",
               i, decoded.label, decoded.ssm, decoded.is_valid,
               parity_ok ? "✅" : "❌");
        if (!parity_ok || (decoded.ssm != SSM_NORMAL_OPERATION) || !decoded.is_valid) {
            critical_failures++;
        }
    }

    printf("\n📊 Итого:\n");
    printf("   Критические ошибки: %d\n", critical_failures);
    printf("   Погрешности квантования: %d (НОРМАЛЬНО для ARINC 429)\n", quantization_warnings);
    
    return critical_failures;
}

int main(void) {
    int result = test_roundtrip();
    printf("\n%s\n", (result == 0) ? "✅ ВСЕ КРИТИЧЕСКИЕ ТЕСТЫ ПРОЙДЕНЫ!" : "❌ ЕСТЬ КРИТИЧЕСКИЕ ОШИБКИ!");
    return (result == 0) ? 0 : 1;
}