/**
 * @file  app_protect.c —— 软件保护层
 *
 * 分层：
 *   L2 硬件比较器 → PROT_TRIP (PA15) / nFAULT (PB12)，固件只做监听
 *   L3 固件：
 *     10 kHz：OVP 立即、OCP/IOCP 连续 10 ms、nFAULT 连续 3 ms
 *     100 ms：NTC 70/80/90 °C，NTC 断路/短路锁存
 *
 * 返回的故障位由 app_control_fault() 统一执行关断；本模块不改输出引脚。
 */
#include "app_config.h"
#include "app_types.h"
#include "app_protect.h"
#include "bsp.h"

#define FAST_TICKS_PER_MS   10u

static uint16_t s_ocp_ticks;
static uint16_t s_iocp_ticks;
static uint16_t s_nfault_ticks;

void app_protect_init(void)
{
    s_ocp_ticks = 0u;
    s_iocp_ticks = 0u;
    s_nfault_ticks = 0u;
    bsp_prot_trip_clear();
}

void app_protect_clear(void)
{
    app_protect_init();
}

uint32_t app_protect_fast(uint16_t vout_mv, uint16_t iout_ma, uint16_t iin_ma,
                          bool nfault_pin)
{
    uint32_t bits = 0u;

    /* 输出过压：立即关断 */
    if (vout_mv > APP_OVP_MV) {
        bits |= PROT_OVP;
    }

    /* 输出过流：连续 10 ms */
    if (iout_ma > APP_OCP_MA) {
        s_ocp_ticks++;
        if (s_ocp_ticks >= APP_OCP_TRIP_MS * FAST_TICKS_PER_MS) {
            bits |= PROT_OCP;
            s_ocp_ticks = 0u;
        }
    } else {
        s_ocp_ticks = 0u;
    }

    /* 输入过流：连续 10 ms */
    if (iin_ma > APP_IOCP_MA) {
        s_iocp_ticks++;
        if (s_iocp_ticks >= APP_IOCP_TRIP_MS * FAST_TICKS_PER_MS) {
            bits |= PROT_IOCP;
            s_iocp_ticks = 0u;
        }
    } else {
        s_iocp_ticks = 0u;
    }

    /* 功率级 nFAULT：连续 3 ms，滤掉开关噪声 */
    if (nfault_pin) {
        if (s_nfault_ticks < 0xFFFFu) {
            s_nfault_ticks++;
        }
        if (s_nfault_ticks >= 3u * FAST_TICKS_PER_MS) {
            bits |= PROT_NFAULT;
            s_nfault_ticks = 0u;
        }
    } else {
        s_nfault_ticks = 0u;
    }

    /* 硬件比较器通过 EXTI 置位：立即视为故障 */
    if (bsp_prot_trip_active()) {
        bits |= PROT_HW_TRIP;
        bsp_prot_trip_clear();
    }

    return bits;
}

uint32_t app_protect_slow(int16_t t_case_x10, bool ntc_ok)
{
    uint32_t bits = 0u;

    if (!ntc_ok) {
        bits |= PROT_NTC_FAULT;          /* 断路/短路：直接锁存 */
        return bits;
    }
    if (t_case_x10 >= APP_NTC_LATCH_X10) {
        bits |= PROT_NTC_LATCH;
    } else if (t_case_x10 >= APP_NTC_SHUT_X10) {
        bits |= PROT_NTC_SHUT;
    }
    return bits;
}
