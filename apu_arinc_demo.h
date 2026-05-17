#ifndef APU_ARINC_DEMO_H
#define APU_ARINC_DEMO_H

#include <stdint.h>
#include <stdbool.h>

/* СТРУКТУРА ОТВЕТА СДУ (RCS_response). Используется для демонстрации ARINC 429 в сценарии 0 */
typedef struct {
    float egt_celsius;          /* BNR: температура выхлопа, °C */
    float rotor_n_percent;      /* BCD: обороты ротора, % (0-120) */
    struct {
        bool gen_on;            /* 1. Генератор подключен к шине */
        bool bleed_open;        /* 2. Клапан отбора открыт */
        bool fault_flag;        /* 3. Флаг аварии APU FAULT */
        bool ready_to_start;    /* 4. Готовность к запуску */
        bool running;           /* 5. ВСУ работает */
        bool test_mode;         /* 6. Тестовый режим */
        bool emergency_stop;    /* 7. Аварийная остановка */
    } discrete;
} RCS_response_t;

/* ПРОТОТИП ФУНКЦИИ ДЕМОСТРАЦИИ ARINC 429 */
void arinc_process_rsc_response(const RCS_response_t* raw);

#endif /* APU_ARINC_DEMO_H */
