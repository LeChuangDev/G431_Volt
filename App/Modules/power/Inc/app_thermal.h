/**
 * @file  app_thermal.h —— 动态热降额与功率包络
 */
#ifndef __APP_THERMAL_H
#define __APP_THERMAL_H

#include <stdint.h>
#include <stdbool.h>

void     app_thermal_init(void);

/* 当前允许功率（0.1 W），按壳温与屏幕亮灭查表线性插值 */
uint16_t app_thermal_pmax_w10(int16_t t_amb_x10, bool screen_on);

/* 由功率包络反算电流上限（mA）；返回 0 表示不允许输出 */
uint16_t app_thermal_current_limit(uint16_t v_set_mv, int16_t t_amb_x10,
                                   bool screen_on, bool *thermal_binding);

#endif /* __APP_THERMAL_H */
