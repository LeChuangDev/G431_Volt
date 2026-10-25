/**
 * @file  app_thermal.c —— 功率包络查表 + 线性插值
 *
 * ★ 下表是【按 25 °C 锚点 + 线性降额】给出的工程占位值，必须用实测风道/
 *   外壳数据重新标定： 25 °C 屏亮 42.8 W、屏灭 52.3 W，80 °C 以上降为 0。
 *   改表只改本文件，不影响控制逻辑。
 */
#include "app_config.h"
#include "app_thermal.h"

#define TEMP_POINTS 9u

static const int16_t s_temp_x10[TEMP_POINTS] = {
    -200, 0, 250, 400, 500, 600, 700, 800, 900
};

/* 单位 0.1 W */
static const uint16_t s_pmax_ui_on[TEMP_POINTS] = {
    428, 428, 428, 380, 330, 270, 200, 100, 0
};

static const uint16_t s_pmax_ui_off[TEMP_POINTS] = {
    523, 523, 523, 470, 410, 340, 260, 140, 0
};

void app_thermal_init(void)
{
    /* 表是静态常量，无需初始化 */
}

static uint16_t interpolate(const uint16_t *table, int16_t t_x10)
{
    if (t_x10 <= s_temp_x10[0]) {
        return table[0];
    }
    if (t_x10 >= s_temp_x10[TEMP_POINTS - 1u]) {
        return table[TEMP_POINTS - 1u];
    }
    for (uint32_t i = 0; i < TEMP_POINTS - 1u; i++) {
        if (t_x10 <= s_temp_x10[i + 1u]) {
            int32_t t0 = s_temp_x10[i];
            int32_t t1 = s_temp_x10[i + 1u];
            int32_t p0 = table[i];
            int32_t p1 = table[i + 1u];
            int32_t p = p0 + ((int32_t)(t_x10 - t0) * (p1 - p0)) / (t1 - t0);
            return (uint16_t)((p > 0) ? p : 0);
        }
    }
    return 0u;
}

uint16_t app_thermal_pmax_w10(int16_t t_amb_x10, bool screen_on)
{
    return interpolate(screen_on ? s_pmax_ui_on : s_pmax_ui_off, t_amb_x10);
}

uint16_t app_thermal_current_limit(uint16_t v_set_mv, int16_t t_amb_x10,
                                   bool screen_on, bool *thermal_binding)
{
    uint16_t p_w10 = app_thermal_pmax_w10(t_amb_x10, screen_on);

    /* 低压段用 5 V 计算，避免 V_set→0 时算出天文数字 */
    uint32_t v_for = (v_set_mv > 5000u) ? v_set_mv : 5000u;

    /* I_mA = P_W × 1000 / V_V = (p_w10/10) × 1e6 / v_mV = p_w10 × 1e5 / v_mV */
    uint32_t i_ma = ((uint32_t)p_w10 * 100000u) / v_for;

    if (thermal_binding != 0) {
        *thermal_binding = (t_amb_x10 >= APP_NTC_ALARM_X10);
    }
    return (i_ma > 0xFFFFu) ? 0xFFFFu : (uint16_t)i_ma;
}
