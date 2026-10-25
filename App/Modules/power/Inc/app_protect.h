/**
 * @file  app_protect.h —— L3 固件保护（快环 10 kHz + 慢环 100 ms）
 */
#ifndef __APP_PROTECT_H
#define __APP_PROTECT_H

#include <stdint.h>
#include <stdbool.h>

void     app_protect_init(void);

/* 10 kHz：返回本拍新增的故障位（0 = 正常） */
uint32_t app_protect_fast(uint16_t vout_mv, uint16_t iout_ma, uint16_t iin_ma,
                          bool nfault_pin);

/* 100 ms：NTC / 慢速判定，返回新增故障位 */
uint32_t app_protect_slow(int16_t t_case_x10, bool ntc_ok);

void     app_protect_clear(void);

#endif /* __APP_PROTECT_H */
