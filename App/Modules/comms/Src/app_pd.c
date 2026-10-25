/**
 * @file  app_pd.c —— CH224Q 输入协商
 *
 * 策略（规格要求）：
 *   1. 上电先请求 12 V 默认档；
 *   2. 输出关闭时才允许升档（15/20/28 V），升档依据"用户设定 + 3 V 裕量"；
 *   3. 28 V 输入时把输入限流设到 4.3 A；
 *   4. 用 ADC 的 VIN 作为"协商是否成功"的判据（CH224Q 的 VBUS 只做诊断）；
 *   5. 芯片不应答时退化为"按实测 VIN 运行"，并置 PROT_PD_FAIL，绝不假装成功。
 *
 * ★ CH224Q 是自主 PD sink 控制器，不回读完整 PDO 列表；本模块只写档位索引。
 *   寄存器/档位编码需对照数据手册核对（见 ch224q.h）。
 */
#include "app_config.h"
#include "app_types.h"
#include "app_control.h"
#include "app_pd.h"
#include "bsp.h"

typedef enum {
    PD_ST_IDLE = 0,
    PD_ST_PROBE,
    PD_ST_REQUEST,
    PD_ST_VERIFY,
    PD_ST_READY,
    PD_ST_FALLBACK,
    PD_ST_FAIL
} pd_state_t;

#define PD_PROBE_RETRY      3u
#define PD_VERIFY_TOL_PCT   15u     /* 实测 VIN 允许 ±15 % */
#define PD_VBUS_MIN_MV      4500u
#define PD_LOST_MS          500u

static const uint8_t s_vlist[] = { 5u, 9u, 12u, 15u, 20u, 28u };

static pd_state_t s_state;
static uint8_t  s_retry;
static uint8_t  s_req_v;
static uint8_t  s_try_idx;
static uint32_t s_deadline_ms;
static uint32_t s_vin_low_since;
static bool     s_vin_low_active;
static bool     s_vin_was_ok;

/* 输入限流：按协商电压查表，超出 PD 常规档时保守处理 */
static uint16_t iin_limit_for(uint8_t volts)
{
    if (volts >= 28u) return 4300u;
    if (volts >= 20u) return 3250u;
    return 3000u;
}

static uint8_t pick_voltage(uint16_t vout_mv)
{
    uint32_t need = (uint32_t)vout_mv + APP_PD_HEADROOM_MV;
    for (uint32_t i = 0; i < sizeof(s_vlist) / sizeof(s_vlist[0]); i++) {
        if ((uint32_t)s_vlist[i] * 1000u >= need) {
            return s_vlist[i];
        }
    }
    return 28u;
}

static uint8_t next_voltage(uint8_t cur)
{
    for (uint32_t i = 0; i < sizeof(s_vlist) / sizeof(s_vlist[0]); i++) {
        if (s_vlist[i] > cur) {
            return s_vlist[i];
        }
    }
    return cur;
}

static void publish(uint8_t volts, uint16_t iin_ma)
{
    g_status.pd_volt        = volts;
    g_status.pd_iin_limit_ma = iin_ma;
}

static void enter_state(pd_state_t st, uint32_t now_ms)
{
    s_state = st;
    switch (st) {
    case PD_ST_PROBE:
        s_retry = 0u;
        s_deadline_ms = now_ms + APP_PD_TIMEOUT_MS;
        break;
    case PD_ST_REQUEST:
        s_deadline_ms = now_ms + APP_PD_TIMEOUT_MS;
        break;
    case PD_ST_VERIFY:
    case PD_ST_READY:
    case PD_ST_FALLBACK:
    case PD_ST_FAIL:
    default:
        break;
    }
}

void app_pd_init(void)
{
    s_state           = PD_ST_PROBE;
    s_retry           = 0u;
    s_req_v           = 0u;
    s_try_idx         = 0u;
    s_deadline_ms     = 0u;
    s_vin_low_since   = 0u;
    s_vin_low_active  = false;
    s_vin_was_ok      = false;

    g_status.pd_volt         = 0u;
    g_status.pd_iin_limit_ma = 0u;
    app_control_set_input_ready(false);
}

bool app_pd_input_ready(void)
{
    return (s_state == PD_ST_READY) || (s_state == PD_ST_FALLBACK);
}

uint8_t app_pd_voltage(void)
{
    return g_status.pd_volt;
}

/* 发起一次电压请求（写档位 + 写限流档） */
static int request_voltage(uint8_t volts)
{
    int rc = bsp_pd_set_voltage(volts);
    if (rc == 0) {
        (void)bsp_pd_set_current(iin_limit_for(volts));
    }
    return rc;
}

void app_pd_task(void)
{
    uint32_t now = bsp_tick_ms();
    uint16_t vin = g_status.vin_mv;
    int rc;

    /* ---- 输入掉电检测（所有状态都检查） ---- */
    if (vin < PD_VBUS_MIN_MV) {
        if (s_vin_was_ok) {
            if (!s_vin_low_active) {
                s_vin_low_active = true;
                s_vin_low_since  = now;
            } else if (now - s_vin_low_since > PD_LOST_MS) {
                s_vin_low_active = false;
                s_vin_was_ok     = false;
                publish(0u, 0u);
                app_control_set_input_ready(false);
                if (s_state == PD_ST_READY || s_state == PD_ST_FALLBACK) {
                    enter_state(PD_ST_PROBE, now);   /* 重新探测，等待重新插入 */
                }
            }
        }
    } else {
        s_vin_low_active = false;       /* 短暂跌落恢复后重新计时 */
    }

    switch (s_state) {

    case PD_ST_IDLE:
        enter_state(PD_ST_PROBE, now);
        break;

    case PD_ST_PROBE: {
        uint16_t vbus = 0u;
        rc = bsp_pd_read_vbus_mv(&vbus);      /* 能读通 = 芯片在线 */
        if (rc == 0) {
            /* 芯片在：请求默认 12 V */
            s_try_idx = 2u;
            s_req_v   = APP_PD_DEFAULT_V / 1000u;
            if (request_voltage(s_req_v) == 0) {
                enter_state(PD_ST_REQUEST, now);
            } else {
                s_retry++;
                if (s_retry >= PD_PROBE_RETRY) {
                    enter_state(PD_ST_FALLBACK, now);
                }
            }
        } else {
            s_retry++;
            if (s_retry >= PD_PROBE_RETRY) {
                /* 芯片不应答：若 VIN 本来就有电压，按实测值继续 */
                if (vin >= PD_VBUS_MIN_MV) {
                    enter_state(PD_ST_FALLBACK, now);
                } else {
                    enter_state(PD_ST_FAIL, now);
                }
            }
        }
        break;
    }

    case PD_ST_REQUEST:
        /* 写档位后等源端重新协商，检查实测 VIN */
        if (vin >= (uint32_t)s_req_v * 1000u * (100u - PD_VERIFY_TOL_PCT) / 100u &&
            vin <= (uint32_t)s_req_v * 1000u * (100u + PD_VERIFY_TOL_PCT) / 100u) {
            publish(s_req_v, iin_limit_for(s_req_v));
            s_vin_was_ok = true;
            s_vin_low_since = 0u;
            app_control_set_input_ready(true);
            enter_state(PD_ST_READY, now);
        } else if (now > s_deadline_ms) {
            /* 12 V 没谈成：退 5 V 再试一次，仍失败则按实测 VIN 处理 */
            if (s_req_v == (APP_PD_DEFAULT_V / 1000u)) {
                s_req_v = 5u;
                if (request_voltage(s_req_v) == 0) {
                    s_deadline_ms = now + APP_PD_TIMEOUT_MS;
                } else {
                    enter_state(PD_ST_FALLBACK, now);
                }
            } else {
                if (vin >= PD_VBUS_MIN_MV) {
                    enter_state(PD_ST_FALLBACK, now);
                } else {
                    g_status.prot_flags |= PROT_PD_FAIL;
                    enter_state(PD_ST_FAIL, now);
                }
            }
        }
        break;

    case PD_ST_READY: {
        /* 源还在？ */
        s_vin_was_ok = true;

        /* 源电压掉了 >20 %（例如源端限功率自动降压）：立即按实测值
           更新包络，目标电压会跟着斜坡降下来，不关输出、不静默 */
        if (vin < (uint32_t)s_req_v * 1000u * 80u / 100u) {
            uint8_t v = (uint8_t)(vin / 1000u);
            if (v < 5u) {
                v = 5u;
            }
            s_req_v = v;
            publish(v, iin_limit_for(v));
            s_deadline_ms = now + APP_PD_TIMEOUT_MS;
            enter_state(PD_ST_REQUEST, now);   /* 重新确认 */
            break;
        }

        /* 需要升档吗？——只在输出关闭时改输入电压 */
        uint16_t want_v = g_status.user_v_mv;
        uint8_t  need   = pick_voltage(want_v);
        if (need > s_req_v &&
            app_control_state() != PWR_STATE_ON &&
            app_control_state() != PWR_STATE_START) {
            uint8_t nv = next_voltage(s_req_v);
            if (nv != s_req_v && nv <= 28u) {
                s_req_v = nv;
                if (request_voltage(s_req_v) == 0) {
                    publish(s_req_v, iin_limit_for(s_req_v));
                    app_control_set_input_ready(false);  /* 重新确认后再放行 */
                    s_deadline_ms = now + APP_PD_TIMEOUT_MS;
                    enter_state(PD_ST_REQUEST, now);
                }
            }
        }
        break;
    }

    case PD_ST_FALLBACK: {
        /* 芯片不可用：以实测 VIN 作为源电压，限流保守取 3 A */
        uint8_t v = (uint8_t)(vin / 1000u);
        if (v > 28u) {
            v = 28u;
        }
        if (vin >= PD_VBUS_MIN_MV) {
            publish(v, iin_limit_for(v));
            s_vin_was_ok    = true;
            s_vin_low_since = 0u;
            app_control_set_input_ready(true);
            g_status.prot_flags |= PROT_PD_FAIL;    /* 只上报，不锁存 */
        } else {
            app_control_set_input_ready(false);
        }
        break;
    }

    case PD_ST_FAIL:
    default:
        app_control_set_input_ready(false);
        break;
    }
}
