/**
 * @file  app_types.h —— 跨模块共享的状态与故障定义
 */
#ifndef __APP_TYPES_H
#define __APP_TYPES_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PWR_STATE_INIT = 0,
    PWR_STATE_SAFE_OFF,       /* 输出禁止，等 PD / 用户指令 */
    PWR_STATE_PD_NEGOTIATE,   /* 正在协商输入电压 */
    PWR_STATE_READY,          /* 输入就绪，随时可开输出 */
    PWR_STATE_START,          /* 软启动等待 */
    PWR_STATE_ON,             /* 输出调节中（CV 或 CC） */
    PWR_STATE_DOWN_PROGRAM,   /* 关断：源出停止 + 有源下拉放电 */
    PWR_STATE_FAULT_LATCH     /* 锁存故障，等待清除 */
} pwr_state_t;

typedef enum {
    LOOP_OFF = 0,
    LOOP_CV,
    LOOP_CC
} loop_mode_t;

/* 电流上限"是谁在限制"——必须原样上报给 ESP32，禁止静默降额 */
typedef enum {
    LIMIT_NONE = 0,
    LIMIT_USER,       /* 用户设定值最小 */
    LIMIT_INPUT,      /* 输入源功率/电流限制 */
    LIMIT_THERMAL,    /* 壳温降额 */
    LIMIT_POWER,      /* 功率包络 */
    LIMIT_HW          /* 3.00 A 硬件上限 */
} limit_reason_t;

/* 故障位（遥测 prot_flags） */
#define PROT_OVP            (1u << 0)
#define PROT_OCP            (1u << 1)
#define PROT_IOCP           (1u << 2)
#define PROT_NTC_SHUT       (1u << 3)
#define PROT_NTC_LATCH      (1u << 4)
#define PROT_NTC_FAULT      (1u << 5)   /* NTC 断路/短路 */
#define PROT_HW_TRIP        (1u << 6)   /* PROT_TRIP 比较器 */
#define PROT_NFAULT         (1u << 7)   /* 功率级 nFAULT */
#define PROT_NO_OUTPUT      (1u << 8)
#define PROT_SINK_FAIL      (1u << 9)   /* 放电超时 */
#define PROT_PD_FAIL        (1u << 10)
#define PROT_ADC_FAULT      (1u << 11)
#define PROT_DAC_FAULT      (1u << 12)
#define PROT_BACKFEED       (1u << 13)  /* 使能前输出端已有电压 */

#define PROT_LATCH_MASK     (PROT_NTC_LATCH | PROT_NTC_FAULT | PROT_HW_TRIP)

typedef struct {
    /* 实时状态（快环写，主循环/中环读；16-bit 对齐访问天然原子） */
    volatile uint32_t     prot_flags;
    volatile pwr_state_t  state;
    volatile loop_mode_t  loop;
    volatile bool         output_on;
    volatile bool         derated;
    volatile uint8_t      limit_reason;
    volatile bool         screen_on;
    volatile bool         wireless_alive;
    volatile bool         ntc_ok;
    volatile uint32_t     uptime_ms;

    volatile uint16_t     user_v_mv;
    volatile uint16_t     user_i_ma;
    volatile uint16_t     target_v_mv;
    volatile uint16_t     ramp_v_mv;
    volatile uint16_t     applied_v_mv;
    volatile uint16_t     vset_code;
    volatile uint16_t     iset_code;

    volatile uint16_t     i_limit_ma;
    volatile uint16_t     p_limit_w10;
    volatile uint16_t     vout_mv;
    volatile uint16_t     iout_ma;
    volatile uint16_t     vin_mv;
    volatile uint16_t     iin_ma;
    volatile int16_t      t_case_x10;
    volatile int16_t      t_amb_x10;

    volatile uint8_t      pd_volt;
    volatile uint16_t     pd_iin_limit_ma;
    volatile uint8_t      cal_ver;
    volatile uint8_t      hb_counter;
} app_status_t;

extern app_status_t g_status;

#endif /* __APP_TYPES_H */
