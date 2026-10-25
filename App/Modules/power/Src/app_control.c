/**
 * @file  app_control.c —— 电源状态机 / CV-CC 控制律 / dV-dt / 有源下拉
 *
 * 实时分工：
 *   10 kHz 快环（TIM6，最高优先级）
 *     - 状态机推进、50 mV/100 µs 斜率、DAC 写（SPI1 只在这里碰）
 *     - 软启动时序、有源下拉时序、关断超时
 *   1 kHz 中环（SysTick）
 *     - 目标电压仲裁（PD 源电压 − 裕量）
 *     - I_final = min(I_user, I_input, P_thermal/V_set, 3.00 A)
 *     - CV/CC 判定与 CC 预修正（改变快环使用的 applied 电压）
 *
 * ★ 所有 DAC 写只允许发生在快环里；中环只改 16-bit 的 volatile 变量。
 *
 * 上电安全：
 *   外部 DAC 复位码 0 对应 VDAC=0 → VOUT=45 V，所以进入任何使能动作前
 *   必须先把 VSET 写成 0 V 对应码（58982）并确认写入成功。
 */
#include "app_config.h"
#include "app_types.h"
#include "app_control.h"
#include "app_signal.h"
#include "app_thermal.h"
#include "app_protect.h"
#include "bsp.h"

#define FAST_TICKS_PER_MS       10u
#define CC_DEBOUNCE_MS          5u
#define DAC_FAIL_LIMIT          10u

/* ---- 命令侧（volatile，跨上下文） ---- */
static volatile uint16_t s_user_v_mv;
static volatile uint16_t s_user_i_ma;
static volatile bool     s_out_req;
static volatile bool     s_input_ready;

/* ---- 中环算出的目标 ---- */
static volatile uint16_t s_target_v_mv;
static volatile uint16_t s_i_limit_ma;

/* ---- 快环私有 ---- */
static volatile pwr_state_t s_state;
static pwr_state_t s_after_down;
static uint16_t s_ramp_mv;
static uint16_t s_applied_mv;
static int16_t  s_cc_trim_mv;
static uint16_t s_state_ticks;
static uint16_t s_down_hold;
static uint8_t  s_down_phase;
static uint32_t s_on_ticks;
static bool     s_output_on;

/* ---- DAC 影子寄存器（写入成功才更新） ---- */
static uint16_t s_vset_code;
static uint16_t s_iset_code;
static bool     s_vset_valid;
static bool     s_iset_valid;
static uint8_t  s_dac_fail;
static uint8_t  s_cc_cnt;
static uint8_t  s_cv_cnt;
static uint16_t s_ictrl_mv;

/* ------------------------------------------------------------------ */
/* 内部工具                                                            */
/* ------------------------------------------------------------------ */
static void enter_down(pwr_state_t after)
{
    bsp_out_enable(false);              /* ① 先停源出（PB10 拉高） */
    s_output_on          = false;
    g_status.output_on   = false;
    g_status.loop        = LOOP_OFF;

    s_state        = PWR_STATE_DOWN_PROGRAM;
    s_after_down   = after;
    s_down_phase   = 0u;                /* ② 下一个快拍才开下拉 */
    s_down_hold    = 0u;
    s_state_ticks  = 0u;
    s_ramp_mv      = 0u;
    s_applied_mv   = 0u;
    s_cc_trim_mv   = 0;
    s_cc_cnt       = 0u;
    s_cv_cnt       = 0u;
    g_status.state = s_state;
}

/* 写外部 DAC；成功返回 0，失败返回 -1（下拍重试） */
static int dac_write_vset(uint16_t code)
{
    if (s_vset_valid && code == s_vset_code) {
        return 0;
    }
    if (bsp_vdac_set_vset_code(code) == 0) {
        s_vset_code  = code;
        s_vset_valid = true;
        s_dac_fail   = 0u;
        g_status.vset_code = code;
        return 0;
    }
    if (++s_dac_fail >= DAC_FAIL_LIMIT) {
        s_dac_fail = 0u;
        app_control_fault(PROT_DAC_FAULT);
    }
    return -1;
}

static int dac_write_iset(uint16_t code)
{
    if (s_iset_valid && code == s_iset_code) {
        return 0;
    }
    if (bsp_vdac_set_iset_code(code) == 0) {
        s_iset_code  = code;
        s_iset_valid = true;
        s_dac_fail   = 0u;
        g_status.iset_code = code;
        return 0;
    }
    if (++s_dac_fail >= DAC_FAIL_LIMIT) {
        s_dac_fail = 0u;
        app_control_fault(PROT_DAC_FAULT);
    }
    return -1;
}

/* 下环完成（或空闲）时把设定拉回 0 V，保证下次上电/使能安全 */
static void force_zero_setpoint(void)
{
    uint16_t code = app_signal_vset_code(0u);
    (void)dac_write_vset(code);
    (void)dac_write_iset(app_signal_iset_code(0u));
}

/* ------------------------------------------------------------------ */
/* 对外命令                                                            */
/* ------------------------------------------------------------------ */
void app_control_init(void)
{
    s_state        = PWR_STATE_SAFE_OFF;
    s_after_down   = PWR_STATE_SAFE_OFF;
    s_user_v_mv    = 0u;
    s_user_i_ma    = 0u;
    s_out_req      = false;
    s_input_ready  = false;
    s_target_v_mv  = 0u;
    s_i_limit_ma   = 0u;
    s_ramp_mv      = 0u;
    s_applied_mv   = 0u;
    s_cc_trim_mv   = 0;
    s_state_ticks  = 0u;
    s_down_hold    = 0u;
    s_down_phase   = 0u;
    s_on_ticks     = 0u;
    s_output_on    = false;
    s_vset_valid   = false;
    s_iset_valid   = false;
    s_dac_fail     = 0u;
    s_cc_cnt       = 0u;
    s_cv_cnt       = 0u;
    s_ictrl_mv     = 0u;

    g_status.state          = s_state;
    g_status.loop           = LOOP_OFF;
    g_status.output_on      = false;
    g_status.derated        = false;
    g_status.limit_reason   = LIMIT_NONE;
    g_status.user_v_mv      = 0u;
    g_status.user_i_ma      = 0u;
    g_status.target_v_mv    = 0u;
    g_status.ramp_v_mv      = 0u;
    g_status.applied_v_mv   = 0u;
    g_status.i_limit_ma     = 0u;
    g_status.p_limit_w10    = 0u;

    bsp_out_enable(false);
    bsp_sink_enable(false);

    /* 关键：先把 0 V 码写进去并确认，否则不允许使能 */
    s_vset_code = app_signal_vset_code(0u);
    if (bsp_vdac_set_vset_code(s_vset_code) == 0) {
        s_vset_valid = true;
        g_status.vset_code = s_vset_code;
    }
    s_iset_code = app_signal_iset_code(0u);
    if (bsp_vdac_set_iset_code(s_iset_code) == 0) {
        s_iset_valid = true;
        g_status.iset_code = s_iset_code;
    }
}

void app_control_set_voltage(uint16_t mv)
{
    if (mv > APP_VOUT_MAX_MV) {
        mv = APP_VOUT_MAX_MV;
    }
    s_user_v_mv = mv;
    g_status.user_v_mv = mv;
}

void app_control_set_current(uint16_t ma)
{
    if (ma > APP_USER_I_MAX_MA) {
        ma = APP_USER_I_MAX_MA;
    }
    s_user_i_ma = ma;
    g_status.user_i_ma = ma;
}

void app_control_request_output(bool on)
{
    s_out_req = on;
}

void app_control_set_input_ready(bool ready)
{
    s_input_ready = ready;
    if (!ready) {
        s_out_req = false;      /* 输入掉电：快环会走安全关断流程 */
    }
}

bool app_control_can_start(void)
{
    return s_input_ready;
}

pwr_state_t app_control_state(void)
{
    return s_state;
}

void app_control_clear_faults(void)
{
    /* 只允许在故障条件消失后清除；清除动作由显式命令触发 */
    if (s_state != PWR_STATE_FAULT_LATCH) {
        return;                 /* 放电还没结束：等它自己走完，不要抢状态 */
    }
    if (g_status.vout_mv > APP_BACKFEED_MV || !g_status.ntc_ok) {
        return;                 /* 故障条件还在，不允许清除 */
    }
    bsp_out_enable(false);
    bsp_sink_enable(false);     /* 放电超时后 SINK 可能仍被保持 */

    /* 先恢复安全设定，再放行状态机（避免与快环的 DAC 写交叠） */
    force_zero_setpoint();

    g_status.prot_flags = 0u;
    app_protect_clear();

    s_state        = s_input_ready ? PWR_STATE_READY : PWR_STATE_SAFE_OFF;
    s_after_down   = s_state;
    s_down_phase   = 0u;
    s_down_hold    = 0u;
    s_state_ticks  = 0u;
    g_status.state = s_state;
}

/* ------------------------------------------------------------------ */
/* 故障入口 —— 任意上下文可调，立即停止源出                            */
/* ------------------------------------------------------------------ */
void app_control_fault(uint32_t bits)
{
    if (bits == 0u) {
        return;
    }
    g_status.prot_flags |= bits;
    s_out_req = false;

    if (s_state == PWR_STATE_FAULT_LATCH) {
        bsp_out_enable(false);
        return;
    }
    if (s_state == PWR_STATE_DOWN_PROGRAM) {
        s_after_down = PWR_STATE_FAULT_LATCH;
        return;
    }

    bsp_out_enable(false);              /* 先停源出 */
    s_output_on        = false;
    g_status.output_on = false;
    g_status.loop      = LOOP_OFF;

    s_state        = PWR_STATE_DOWN_PROGRAM;
    s_after_down   = PWR_STATE_FAULT_LATCH;
    s_down_phase   = 0u;
    s_down_hold    = 0u;
    s_state_ticks  = 0u;
    s_cc_trim_mv   = 0;
    g_status.state = s_state;
}

/* ------------------------------------------------------------------ */
/* 10 kHz 快环                                                         */
/* ------------------------------------------------------------------ */
void app_control_fast(void)
{
    uint16_t vout = g_status.vout_mv;

    switch (s_state) {

    case PWR_STATE_INIT:
    case PWR_STATE_PD_NEGOTIATE:
    case PWR_STATE_SAFE_OFF:
    case PWR_STATE_READY:
        if (s_state != PWR_STATE_READY && s_input_ready) {
            s_state = PWR_STATE_READY;
            g_status.state = s_state;
        }
        if (s_out_req && s_input_ready) {
            /* s_out_req 保持为真：它是"用户希望输出开"的电平，不是脉冲 */
            s_state_ticks = 0u;
            s_ramp_mv     = 0u;
            s_applied_mv  = 0u;
            s_cc_trim_mv  = 0;
            s_state       = PWR_STATE_START;
            g_status.state = s_state;
        }
        break;

    case PWR_STATE_START:
        s_state_ticks++;

        /* ① 等硬件 SS 引脚把软启动电容充到位 */
        if (s_state_ticks <= APP_SS_WAIT_MS * FAST_TICKS_PER_MS) {
            break;
        }

        /* ② 使能前必须确认 0 V 设定已经写入 DAC */
        if (!s_vset_valid) {
            if (dac_write_vset(app_signal_vset_code(0u)) < 0) {
                if (s_state_ticks > (APP_SS_WAIT_MS + APP_START_TIMEOUT_MS) * FAST_TICKS_PER_MS) {
                    app_control_fault(PROT_DAC_FAULT);
                }
                break;
            }
        }
        /* ③ 电流上限必须在使能之前就位：否则开启瞬间模拟 CC 环参考为 0 */
        if (dac_write_iset(app_signal_iset_code(s_i_limit_ma)) < 0) {
            if (s_state_ticks > (APP_SS_WAIT_MS + APP_START_TIMEOUT_MS) * FAST_TICKS_PER_MS) {
                app_control_fault(PROT_DAC_FAULT);
            }
            break;
        }

        /* ④ 输出端不能有倒灌电压 */
        if (vout > APP_BACKFEED_MV) {
            app_control_fault(PROT_BACKFEED);
            break;
        }

        /* ⑤ 使能功率级 */
        bsp_out_enable(true);
        s_output_on        = true;
        g_status.output_on = true;
        s_ramp_mv          = 0u;
        s_applied_mv       = 0u;
        s_on_ticks         = 0u;
        s_state            = PWR_STATE_ON;
        g_status.state     = s_state;
        break;

    case PWR_STATE_ON: {
        s_on_ticks++;

        /* 输入掉电或用户撤请求：立即安全关断 */
        if (!s_input_ready) {
            enter_down(PWR_STATE_SAFE_OFF);
            break;
        }
        if (!s_out_req) {
            enter_down(PWR_STATE_READY);
            break;
        }

        /* dV/dt：每拍最多 50 mV */
        if (s_ramp_mv < s_target_v_mv) {
            uint32_t next = (uint32_t)s_ramp_mv + APP_DV_PER_TICK_MV;
            s_ramp_mv = (next > s_target_v_mv) ? s_target_v_mv : (uint16_t)next;
        } else if (s_ramp_mv > s_target_v_mv) {
            int32_t next = (int32_t)s_ramp_mv - (int32_t)APP_DV_PER_TICK_MV;
            s_ramp_mv = (next < (int32_t)s_target_v_mv) ? s_target_v_mv : (uint16_t)next;
        }

        /* CC 预修正：实际输出 = 斜率值 − trim */
        int32_t applied = (int32_t)s_ramp_mv - (int32_t)s_cc_trim_mv;
        if (applied < 0) {
            applied = 0;
        }
        s_applied_mv = (uint16_t)applied;

        if (dac_write_vset(app_signal_vset_code(s_applied_mv)) < 0) {
            if (s_state != PWR_STATE_ON) {
                break;              /* 写失败已触发故障 */
            }
        }
        (void)dac_write_iset(app_signal_iset_code(s_i_limit_ma));
        if (s_state != PWR_STATE_ON) {
            break;
        }

        /* 建立输出超时：目标不低但电压上不来 */
        if (s_target_v_mv > 5000u &&
            s_on_ticks > APP_START_TIMEOUT_MS * FAST_TICKS_PER_MS &&
            vout < APP_NO_OUTPUT_MV) {
            app_control_fault(PROT_NO_OUTPUT);
            break;
        }

        break;
    }

    case PWR_STATE_DOWN_PROGRAM:
        s_state_ticks++;

        /* ① 停源出后的下一拍再开下拉，保证 OUT_EN 先动作 */
        if (s_down_phase == 0u) {
            bsp_sink_enable(true);
            s_down_phase = 1u;
            break;
        }

        /* ② 放电到阈值以下并保持 10 ms */
        if (vout < APP_DOWN_DONE_MV) {
            if (++s_down_hold >= APP_DOWN_HOLD_MS * FAST_TICKS_PER_MS) {
                bsp_sink_enable(false);
                bsp_out_enable(false);
                force_zero_setpoint();
                s_ramp_mv    = 0u;
                s_applied_mv = 0u;
                s_cc_trim_mv = 0;
                s_state      = (s_after_down == PWR_STATE_READY && !s_input_ready)
                               ? PWR_STATE_SAFE_OFF : s_after_down;
                s_after_down = s_state;
                g_status.state = s_state;
            }
        } else {
            s_down_hold = 0u;
        }

        /* ③ 放电超时：下拉保持，锁存 */
        if (s_state == PWR_STATE_DOWN_PROGRAM &&
            s_state_ticks > APP_DOWN_TIMEOUT_MS * FAST_TICKS_PER_MS) {
            g_status.prot_flags |= PROT_SINK_FAIL;
            g_status.state = PWR_STATE_FAULT_LATCH;
            s_after_down   = PWR_STATE_FAULT_LATCH;
            s_state        = PWR_STATE_FAULT_LATCH;
            /* SINK 保持导通，避免电容电压长时间挂在下一次使能上 */
        }
        break;

    case PWR_STATE_FAULT_LATCH:
    default:
        bsp_out_enable(false);
        s_output_on        = false;
        g_status.output_on = false;
        g_status.loop      = LOOP_OFF;
        break;
    }

    g_status.state = s_state;
}

/* ------------------------------------------------------------------ */
/* 1 kHz 中环                                                          */
/* ------------------------------------------------------------------ */
void app_control_medium(void)
{
    /* ---------- 1. 目标电压 ---------- */
    uint32_t uv = s_user_v_mv;
    if (uv > APP_VOUT_MAX_MV) {
        uv = APP_VOUT_MAX_MV;
    }

    uint32_t pd_mv = (uint32_t)g_status.pd_volt * 1000u;
    uint32_t vmax  = APP_VOUT_MAX_MV;
    if (pd_mv > APP_PD_HEADROOM_MV) {
        uint32_t lim = pd_mv - APP_PD_HEADROOM_MV;
        if (lim < vmax) {
            vmax = lim;
        }
    } else {
        vmax = 0u;      /* 还没有有效 PD 电压 */
    }
    if (uv > vmax) {
        uv = vmax;
    }
    s_target_v_mv     = (uint16_t)uv;
    g_status.target_v_mv = (uint16_t)uv;

    /* ---------- 2. 限流仲裁 ---------- */
    uint32_t i_user = s_user_i_ma;
    if (i_user > APP_USER_I_MAX_MA) {
        i_user = APP_USER_I_MAX_MA;
    }

    uint16_t v_for = (uv > 5000u) ? (uint16_t)uv : 5000u;
    uint32_t i_final = i_user;
    uint8_t  reason  = LIMIT_USER;

    /* 功率包络 / 壳温 */
    bool thermal_binding = false;
    uint16_t p_w10 = app_thermal_pmax_w10(g_status.t_amb_x10, g_status.screen_on);
    uint16_t i_thermal = app_thermal_current_limit(v_for, g_status.t_amb_x10,
                                                   g_status.screen_on, &thermal_binding);
    g_status.p_limit_w10 = p_w10;
    if ((uint32_t)i_thermal < i_final) {
        i_final = i_thermal;
        reason  = thermal_binding ? LIMIT_THERMAL : LIMIT_POWER;
    }

    /* 输入源能力：I_out = Vin × Iin_limit × η / Vout */
    if (g_status.pd_iin_limit_ma > 0u && g_status.vin_mv > 0u) {
        uint32_t i_in = (uint32_t)g_status.vin_mv * g_status.pd_iin_limit_ma / v_for;
        i_in = i_in * APP_IN_EFF_PCT / 100u;
        if (i_in < i_final) {
            i_final = i_in;
            reason  = LIMIT_INPUT;
        }
    }

    /* 3.00 A 硬上限最后兜底 */
    if (i_final > APP_I_HW_MAX_MA) {
        i_final = APP_I_HW_MAX_MA;
        reason  = LIMIT_HW;
    }

    if (i_final > 0xFFFFu) {
        i_final = 0xFFFFu;
    }
    s_i_limit_ma          = (uint16_t)i_final;
    g_status.i_limit_ma   = (uint16_t)i_final;
    g_status.limit_reason = (s_state == PWR_STATE_ON) ? reason : LIMIT_NONE;
    g_status.derated      = (s_state == PWR_STATE_ON) && (reason != LIMIT_USER);

    /* ---------- 3. 输入限流 DAC（内部 DAC1 / PA4 ICTRL） ---------- */
    uint32_t ictrl_mv = (uint32_t)g_status.pd_iin_limit_ma * BSP_ICTRL_FULL_MV / BSP_ICTRL_FULL_MA;
    if (ictrl_mv > BSP_ICTRL_FULL_MV) {
        ictrl_mv = BSP_ICTRL_FULL_MV;
    }
    if ((uint32_t)(s_ictrl_mv > ictrl_mv ? s_ictrl_mv - ictrl_mv : ictrl_mv - s_ictrl_mv) >= 5u) {
        s_ictrl_mv = (uint16_t)ictrl_mv;
        bsp_dac_set_mv(ictrl_mv);
    }

    /* ---------- 4. CV / CC 判定与预修正 ---------- */
    if (s_state == PWR_STATE_ON && s_output_on) {
        bool cc = (s_i_limit_ma > 0u) &&
                  (g_status.iout_ma + APP_CC_I_MARGIN_MA >= s_i_limit_ma) &&
                  ((uint32_t)g_status.vout_mv + APP_CC_V_MARGIN_MV < s_applied_mv);

        if (cc) {
            if (s_cc_cnt < CC_DEBOUNCE_MS) {
                s_cc_cnt++;
            }
            s_cv_cnt = 0u;
        } else {
            if (s_cv_cnt < CC_DEBOUNCE_MS) {
                s_cv_cnt++;
            }
            s_cc_cnt = 0u;
        }

        if (s_cc_cnt >= CC_DEBOUNCE_MS) {
            g_status.loop = LOOP_CC;
            if (s_cc_trim_mv < (int16_t)APP_CC_TRIM_MAX_MV) {
                s_cc_trim_mv += (int16_t)APP_CC_TRIM_STEP_MV;
            }
        } else if (s_cv_cnt >= CC_DEBOUNCE_MS) {
            g_status.loop = LOOP_CV;
            if (s_cc_trim_mv > 0) {
                s_cc_trim_mv -= (int16_t)APP_CC_TRIM_STEP_MV;
            }
        }
    } else {
        g_status.loop = LOOP_OFF;
        s_cc_trim_mv  = 0;
        s_cc_cnt      = 0u;
        s_cv_cnt      = 0u;
    }

    g_status.ramp_v_mv = s_ramp_mv;
    g_status.applied_v_mv = s_applied_mv;
}
