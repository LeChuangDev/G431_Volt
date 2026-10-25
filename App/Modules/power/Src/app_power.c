/**
 * @file  app_power.c —— 实时调度
 *
 *   TIM6 10 kHz（优先级 0）  bsp_fast_tick_hook()
 *       启动下一次注入转换 → 读上一轮结果 → 换算 → 保护 → 控制 → 波形采样
 *   SysTick 1 kHz（优先级 15） bsp_tick_1ms_hook()
 *       控制中环 + 通信装帧/心跳
 *   主循环
 *       100 ms 慢环（NTC/热降额/PD/指示灯）+ I2C 命令 + 看门狗
 *
 * 看门狗只在"快环确实在跑"时才喂：任一实时环卡死都会在 200 ms 内复位。
 */
#include "app_config.h"
#include "app_types.h"
#include "app_power.h"
#include "app_control.h"
#include "app_signal.h"
#include "app_thermal.h"
#include "app_protect.h"
#include "app_comms.h"
#include "app_pd.h"
#include "bsp.h"
#include <string.h>

app_status_t g_status;

static volatile bool     s_ready;
static volatile uint16_t s_ntc_raw;
static uint8_t  s_led_div;
static bool     s_led_level;

/* ------------------------------------------------------------------ */
static uint16_t clamp_u16(uint32_t v, uint16_t max)
{
    return (v > max) ? max : (uint16_t)v;
}

/* ------------------------------------------------------------------ */
void app_power_init(void)
{
    memset((void *)&g_status, 0, sizeof(g_status));

    app_signal_init();
    app_thermal_init();
    app_protect_init();
    app_control_init();
    app_comms_init();
    app_pd_init();

    s_ntc_raw  = 0u;
    s_led_div  = 0u;
    s_led_level = false;

    g_status.ntc_ok   = true;
    g_status.cal_ver  = app_signal_version();
    g_status.state    = PWR_STATE_SAFE_OFF;

    s_ready = true;
}

/* ------------------------------------------------------------------ */
/* 10 kHz                                                              */
/* ------------------------------------------------------------------ */
void bsp_fast_tick_hook(void)
{
    if (!s_ready) {
        return;
    }

    bsp_adc_raw_t raw;
    bsp_adc_read(&raw);         /* 上一拍启动的转换结果 */
    bsp_adc_start();            /* 启动下一拍 */

    if (!bsp_adc_valid()) {
        return;
    }

    uint16_t vout = clamp_u16(app_signal_vout_mv(raw.vout), 60000u);
    uint16_t iout = clamp_u16(app_signal_iout_ma(raw.iout), 60000u);
    uint16_t vin  = clamp_u16(app_signal_vin_mv(raw.vin), 60000u);
    uint16_t iin  = clamp_u16(app_signal_iin_ma(raw.iin), 60000u);

    g_status.vout_mv = vout;
    g_status.iout_ma = iout;
    g_status.vin_mv  = vin;
    g_status.iin_ma  = iin;
    s_ntc_raw        = raw.ntc;

    /* L3 快保护：OVP 立即，OCP/IOCP 10 ms，nFAULT/PROT_TRIP 滤波 */
    uint32_t bits = app_protect_fast(vout, iout, iin, bsp_nfault_asserted());
    if (bits != 0u) {
        /* 本拍只执行关断；下一拍（100 µs 后）状态机再打开 SINK，
           保证"先停源出、后有源下拉"的先后顺序 */
        app_control_fault(bits);
    } else {
        /* 控制律（内部按需写外部 DAC） */
        app_control_fast();
    }

    /* 波形采样（10 kHz 里每 32 拍存一点） */
    app_comms_sample(vout, iout);
}

/* ------------------------------------------------------------------ */
/* 1 kHz                                                               */
/* ------------------------------------------------------------------ */
void bsp_tick_1ms_hook(void)
{
    if (!s_ready) {
        return;
    }
    uint32_t now = bsp_tick_ms();
    g_status.uptime_ms = now;

    app_control_medium();       /* 限流仲裁 / CV-CC 判定 */
    app_comms_medium(now);      /* 遥测帧 / 波形帧 / 心跳 */
}

/* ------------------------------------------------------------------ */
/* 指示灯：亮=输出中，慢闪=就绪，快闪=故障，灭=安全关断                */
/* ------------------------------------------------------------------ */
static void led_update(void)
{
    if (g_status.prot_flags != 0u) {
        if (++s_led_div >= 1u) {                 /* 100 ms 翻转 */
            s_led_div = 0u;
            s_led_level = !s_led_level;
        }
    } else if (g_status.output_on) {
        s_led_div = 0u;
        s_led_level = true;
    } else if (app_control_state() == PWR_STATE_READY) {
        if (++s_led_div >= 5u) {                 /* 500 ms 翻转 */
            s_led_div = 0u;
            s_led_level = !s_led_level;
        }
    } else {
        s_led_div = 0u;
        s_led_level = false;
    }

    if (s_led_level) {
        bsp_led_on();
    } else {
        bsp_led_off();
    }
}

/* ------------------------------------------------------------------ */
/* 100 ms                                                              */
/* ------------------------------------------------------------------ */
static void slow_tasks(void)
{
    /* 唯一一路 NTC：既作为壳温（保护/温漂）也作为环境温度（热降额）。
       ★ 若硬件实际有两路 NTC，这里需要拆成两个通道。 */
    int16_t t_x10 = g_status.t_case_x10;
    bool ok = app_signal_ntc_x10(s_ntc_raw, &t_x10);
    g_status.ntc_ok = ok;
    if (ok) {
        g_status.t_case_x10 = t_x10;
        g_status.t_amb_x10  = t_x10;
    }

    uint32_t bits = app_protect_slow(g_status.t_case_x10, ok);
    if (bits != 0u) {
        app_control_fault(bits);
    }

    /* PD 协商（阻塞式 I2C，只在主循环里跑） */
    app_pd_task();

    led_update();

    /* 校准数据若被命令改动，版本号会变；这里只同步显示值 */
    g_status.cal_ver = app_signal_version();
}

/* ------------------------------------------------------------------ */
void app_power_run(void)
{
    uint32_t last_fast = bsp_fast_tick_count();
    uint32_t last_slow = bsp_tick_ms();

    for (;;) {
        uint32_t now = bsp_tick_ms();
        uint32_t f   = bsp_fast_tick_count();

        /* 快环有推进才喂狗：实时环停摆 → 200 ms 内复位 */
        if (f != last_fast) {
            last_fast = f;
            bsp_iwdg_feed();
        }

        /* ESP32 命令 + SPI2 从机 DMA 恢复 */
        app_comms_poll();

        if (now - last_slow >= APP_SLOW_MS) {
            last_slow += APP_SLOW_MS;
            if (now - last_slow > 2u * APP_SLOW_MS) {
                last_slow = now;        /* 被长事务拖住时不要补跑多次 */
            }
            slow_tasks();
        }
    }
}
