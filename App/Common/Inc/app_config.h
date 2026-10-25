/**
 * @file  app_config.h —— 产品级常量（控制/保护/热降额/通信）
 *
 * 所有"可调参数"集中在这里；改行为先改这里，不要散落到各模块。
 * 标定/校准数据不在这里，走 Flash 里的 app_cal_t（见 app_signal.h）。
 */
#ifndef __APP_CONFIG_H
#define __APP_CONFIG_H

/* ---- 节拍 ---- */
#define APP_FAST_HZ                 10000u   /* TIM6 快环 100 µs */
#define APP_MEDIUM_HZ               1000u    /* SysTick 中环 1 ms */
#define APP_SLOW_MS                 100u     /* 慢环 100 ms */

/* ---- 输出电压 ---- */
#define APP_VOUT_MAX_MV             36000u   /* 软件设定上限 36 V */
#define APP_OVP_MV                  37500u   /* 输出过压保护 37.5 V */
#define APP_BACKFEED_MV             1500u    /* 使能前若 VOUT 高于此值视为倒灌 */

/* ---- 输出电流 ---- */
#define APP_USER_I_MAX_MA           3000u    /* 用户可设上限 3.00 A */
#define APP_I_HW_MAX_MA             3000u    /* 功率级硬上限 */
#define APP_OCP_MA                  3150u    /* 输出过流 3.15 A */
#define APP_OCP_TRIP_MS             10u
#define APP_IOCP_MA                 4300u    /* 输入过流 4.3 A */
#define APP_IOCP_TRIP_MS            10u

/* ---- dV/dt：500 V/s = 50 mV / 100 µs 快拍 ---- */
#define APP_DV_PER_TICK_MV          50u

/* ---- 有源下拉（down-programming）---- */
/* 放到 1 V 以下算放完——比规格要求的 5 V 更严格，保证下次使能前不会倒灌 */
#define APP_DOWN_DONE_MV            1000u
#define APP_DOWN_HOLD_MS            10u
#define APP_DOWN_TIMEOUT_MS         3000u

/* ---- 软启动 ---- */
#define APP_SS_WAIT_MS              20u      /* 等硬件软启动/SS 引脚充电 */
#define APP_START_TIMEOUT_MS        300u     /* 从使能到建立输出的超时 */
#define APP_NO_OUTPUT_MV            1000u    /* 目标 >5 V 时，VOUT 低于此值判无输出 */

/* ---- NTC / 热保护（0.1 °C）---- */
#define APP_NTC_ALARM_X10           700      /* 70 °C 告警 + 降流 */
#define APP_NTC_SHUT_X10            800      /* 80 °C 关断 */
#define APP_NTC_LATCH_X10           900      /* 90 °C 锁存 */

/* ---- 通信 / 看门狗 ---- */
#define APP_HB_TIMEOUT_MS           1000u
#define APP_IWDG_TIMEOUT_MS         200u
#define APP_TELEMETRY_MS            1u
#define APP_WAVE_PERIOD_MS          20u      /* 50 Hz 波形块 */

/* ---- PD 协商 ---- */
#define APP_PD_HEADROOM_MV          3000u    /* 源电压需比输出高 3 V */
#define APP_PD_TIMEOUT_MS           3000u
#define APP_PD_DEFAULT_V            12000u
#define APP_PD_VMAX_MV              28000u
#define APP_IN_EFF_PCT              93u      /* 输入功率换算裕量（%） */

/* ---- CC 修正（软件预调节，防模拟 CV 环积分饱和）---- */
#define APP_CC_V_MARGIN_MV          500u
#define APP_CC_I_MARGIN_MA          20u
#define APP_CC_TRIM_MAX_MV          2000u
#define APP_CC_TRIM_STEP_MV         1u       /* 每 1 ms 最多修正 1 mV */

/* ---- 故障恢复 ---- */
#define APP_AUTO_RETRY_MAX          0u       /* 0 = 所有硬故障锁存，需显式清除 */
#define APP_RETRY_DELAY_MS          1000u

/* ---- 遥测帧 ---- */
#define APP_FRAME_SYNC_TX           0xA5u
#define APP_FRAME_SYNC_RX           0x5Au
#define APP_FRAME_TYPE_TELEM        0x01u
#define APP_FRAME_TYPE_WAVE         0x02u

#endif /* __APP_CONFIG_H */
