/**
 * @file  app_signal.h —— 采样换算、五点校准、DAC 码生成、Flash 存取
 */
#ifndef __APP_SIGNAL_H
#define __APP_SIGNAL_H

#include <stdint.h>
#include <stdbool.h>

/* K1..K7 校准参数编号（协议里下发） */
#define APP_K_V_GAIN        1u
#define APP_K_V_OFFSET      2u
#define APP_K_I_GAIN        3u
#define APP_K_I_OFFSET      4u
#define APP_K_DAC_TEMPCO    5u
#define APP_K_NTC_BETA      6u
#define APP_K_NTC_R25       7u

void     app_signal_init(void);          /* 从 Flash 加载，失败用默认值 */

/* 原始码 → 工程量（已含五点校准或斜率截距） */
uint32_t app_signal_vout_mv(uint16_t raw);
uint32_t app_signal_iout_ma(uint16_t raw);
uint32_t app_signal_vin_mv(uint16_t raw);
uint32_t app_signal_iin_ma(uint16_t raw);
bool     app_signal_ntc_x10(uint16_t raw, int16_t *t_x10);

/* 设定值 → 外部 DAC 码（含温漂补偿） */
uint16_t app_signal_vset_code(uint16_t vout_mv);
uint16_t app_signal_iset_code(uint16_t i_ma);

/* 校准参数读写（返回 0 成功） */
int  app_signal_set_k(uint8_t k, int32_t value);
int  app_signal_get_k(uint8_t k, int32_t *value);
int  app_signal_set_vpoint(uint8_t idx, uint16_t raw, int16_t mv);
int  app_signal_set_ipoint(uint8_t idx, uint16_t raw, int16_t ma);

int  app_signal_save(void);              /* 擦页 + 编程（会短暂关中断） */
void app_signal_defaults(void);
uint8_t app_signal_version(void);

/* NTC 参数（K6/K7 可改） */
uint16_t app_signal_ntc_beta(void);
uint16_t app_signal_ntc_r25(void);

#endif /* __APP_SIGNAL_H */
