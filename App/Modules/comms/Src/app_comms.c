/**
 * @file  app_comms.c —— 与 ESP32 的数据交互
 *
 * 物理通道：
 *   SPI2 从机（G431 只发）：1 ms 遥测帧；每 20 ms 用一帧波形代替
 *   I2C1 从机 0x24：ESP32 下发命令
 *   PC6/PC7：双向 1 kHz 心跳
 *   PC8 DATA_RDY：新帧已装入 SPI 缓冲
 *
 * 帧格式见 app_comms.h / 遥测布局见本文件顶部宏。
 * 心跳丢失 > 1 s：只把 wireless_alive 置 false，输出状态保持不变。
 */
#include "app_config.h"
#include "app_types.h"
#include "app_comms.h"
#include "app_control.h"
#include "app_signal.h"
#include "bsp.h"
#include <string.h>

/* ---- 32 字节帧布局 ---- */
#define TELEM_OFF_SYNC      0u
#define TELEM_OFF_TYPE      1u
#define TELEM_OFF_SEQ       2u
#define TELEM_OFF_FLAGS     4u
#define TELEM_OFF_VSET      6u
#define TELEM_OFF_VOUT      8u
#define TELEM_OFF_IOUT      10u
#define TELEM_OFF_TCASE     12u
#define TELEM_OFF_TAMB      14u
#define TELEM_OFF_VIN       16u
#define TELEM_OFF_IIN       18u
#define TELEM_OFF_ILIM      20u
#define TELEM_OFF_PLIM      22u
#define TELEM_OFF_REASON    24u
#define TELEM_OFF_PDV       25u
#define TELEM_OFF_CALVER    26u
#define TELEM_OFF_HB        27u
#define TELEM_OFF_PROT      28u
#define TELEM_OFF_CRC       30u
#define FRAME_LEN           32u

/* 波形帧：4-5 = 目标电压，6..29 = 6 组 (V,I) */
#define WAVE_OFF_VSET       4u

/* 状态位 */
#define FLAG_OUTPUT_ON      (1u << 0)
#define FLAG_DERATED        (1u << 1)
#define FLAG_WIRELESS       (1u << 2)
#define FLAG_NTC_OK         (1u << 3)
#define FLAG_SCREEN_ON      (1u << 4)
#define FLAG_SPI_LINK       (1u << 5)
#define FLAG_STATE_SHIFT    8u
#define FLAG_LOOP_SHIFT     12u

#define WAVE_SLOTS          6u
#define WAVE_SAMPLE_DIV     32u     /* 100 µs × 32 ≈ 3.2 ms，6 点 ≈ 19.2 ms */

static uint8_t  s_tx[FRAME_LEN];
static uint16_t s_seq;
static uint8_t  s_wave_v[WAVE_SLOTS];
static uint8_t  s_wave_i[WAVE_SLOTS];
static uint8_t  s_wave_idx;
static uint8_t  s_sample_div;

static uint16_t s_hb_edges;
static bool     s_hb_last;
static uint32_t s_hb_last_edge_ms;
static bool     s_hb_out_level;
static bool     s_wireless_alive;

/* ------------------------------------------------------------------ */
/* CRC / 打包                                                          */
/* ------------------------------------------------------------------ */
static uint16_t crc16_ccitt(const uint8_t *d, uint32_t n)
{
    uint16_t crc = 0xFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= (uint16_t)d[i] << 8;
        for (uint32_t b = 0; b < 8u; b++) {
            if (crc & 0x8000u) {
                crc = (uint16_t)((crc << 1) ^ 0x1021u);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_i16(uint8_t *p, int16_t v)
{
    put_u16(p, (uint16_t)v);
}

static void frame_seal(void)
{
    uint16_t crc = crc16_ccitt(s_tx, TELEM_OFF_CRC);
    put_u16(&s_tx[TELEM_OFF_CRC], crc);
}

/* ------------------------------------------------------------------ */
/* 初始化                                                              */
/* ------------------------------------------------------------------ */
void app_comms_init(void)
{
    memset(s_tx, 0, sizeof(s_tx));
    s_seq          = 0u;
    s_wave_idx     = 0u;
    s_sample_div   = 0u;
    s_hb_edges     = 0u;
    s_hb_last      = false;
    s_hb_last_edge_ms = bsp_tick_ms();
    s_hb_out_level = false;
    s_wireless_alive = false;

    g_status.wireless_alive = false;
    g_status.hb_counter = 0u;
    g_status.screen_on = false;
    bsp_data_ready_set(false);
}

bool app_comms_wireless_alive(void)
{
    return s_wireless_alive;
}

/* ------------------------------------------------------------------ */
/* 10 kHz 波形采样                                                     */
/* ------------------------------------------------------------------ */
void app_comms_sample(uint16_t vout_mv, uint16_t iout_ma)
{
    if (++s_sample_div < WAVE_SAMPLE_DIV) {
        return;
    }
    s_sample_div = 0u;
    s_wave_v[s_wave_idx] = (uint8_t)(vout_mv / 128u);   /* 0..511 → ×128 还原 */
    s_wave_i[s_wave_idx] = (uint8_t)(iout_ma / 16u);    /* 0..255 → ×16 还原 */
    s_wave_idx = (uint8_t)((s_wave_idx + 1u) % WAVE_SLOTS);
}

/* ------------------------------------------------------------------ */
/* 遥测帧                                                              */
/* ------------------------------------------------------------------ */
static void build_telemetry(void)
{
    uint16_t flags = 0u;

    if (g_status.output_on)              flags |= FLAG_OUTPUT_ON;
    if (g_status.derated)                flags |= FLAG_DERATED;
    if (s_wireless_alive)                flags |= FLAG_WIRELESS;
    if (g_status.ntc_ok)                 flags |= FLAG_NTC_OK;
    if (g_status.screen_on)              flags |= FLAG_SCREEN_ON;
    if (bsp_spi2_slave_link_ok())        flags |= FLAG_SPI_LINK;
    flags |= (uint16_t)(((uint16_t)g_status.state & 0x0Fu) << FLAG_STATE_SHIFT);
    flags |= (uint16_t)(((uint16_t)g_status.loop & 0x03u) << FLAG_LOOP_SHIFT);

    s_tx[TELEM_OFF_SYNC] = APP_FRAME_SYNC_TX;
    s_tx[TELEM_OFF_TYPE] = APP_FRAME_TYPE_TELEM;
    put_u16(&s_tx[TELEM_OFF_SEQ], ++s_seq);
    put_u16(&s_tx[TELEM_OFF_FLAGS], flags);
    put_u16(&s_tx[TELEM_OFF_VSET], g_status.target_v_mv);
    put_u16(&s_tx[TELEM_OFF_VOUT], g_status.vout_mv);
    put_u16(&s_tx[TELEM_OFF_IOUT], g_status.iout_ma);
    put_i16(&s_tx[TELEM_OFF_TCASE], g_status.t_case_x10);
    put_i16(&s_tx[TELEM_OFF_TAMB], g_status.t_amb_x10);
    put_u16(&s_tx[TELEM_OFF_VIN], g_status.vin_mv);
    put_u16(&s_tx[TELEM_OFF_IIN], g_status.iin_ma);
    put_u16(&s_tx[TELEM_OFF_ILIM], g_status.i_limit_ma);
    put_u16(&s_tx[TELEM_OFF_PLIM], g_status.p_limit_w10);
    s_tx[TELEM_OFF_REASON] = g_status.limit_reason;
    s_tx[TELEM_OFF_PDV]    = g_status.pd_volt;
    s_tx[TELEM_OFF_CALVER] = g_status.cal_ver;
    s_tx[TELEM_OFF_HB]     = (uint8_t)(s_hb_edges & 0xFFu);
    put_u16(&s_tx[TELEM_OFF_PROT], (uint16_t)g_status.prot_flags);
    frame_seal();
}

static void build_wave(void)
{
    s_tx[TELEM_OFF_SYNC] = APP_FRAME_SYNC_TX;
    s_tx[TELEM_OFF_TYPE] = APP_FRAME_TYPE_WAVE;
    put_u16(&s_tx[TELEM_OFF_SEQ], ++s_seq);
    put_u16(&s_tx[WAVE_OFF_VSET], g_status.target_v_mv);

    /* 从最旧样本开始输出，保证时序正确 */
    uint32_t p = 6u;
    for (uint32_t k = 0; k < WAVE_SLOTS; k++) {
        uint32_t idx = (s_wave_idx + k) % WAVE_SLOTS;
        put_u16(&s_tx[p], (uint16_t)((uint16_t)s_wave_v[idx] * 128u));
        p += 2u;
        put_u16(&s_tx[p], (uint16_t)((uint16_t)s_wave_i[idx] * 16u));
        p += 2u;
    }
    frame_seal();
}

/* ------------------------------------------------------------------ */
/* 1 kHz 中环                                                          */
/* ------------------------------------------------------------------ */
void app_comms_medium(uint32_t now_ms)
{
    /* ---- 心跳输入：1 kHz 翻转，丢边沿 >1 s 判死 ---- */
    bool hb = bsp_hb_in_level();
    if (hb != s_hb_last) {
        s_hb_last = hb;
        s_hb_last_edge_ms = now_ms;
        s_hb_edges++;
    }
    if (now_ms - s_hb_last_edge_ms > APP_HB_TIMEOUT_MS) {
        s_wireless_alive = false;       /* 只标记：输出保持不变 */
    } else {
        s_wireless_alive = true;
    }
    g_status.wireless_alive = s_wireless_alive;
    g_status.hb_counter = (uint8_t)(s_hb_edges & 0xFFu);

    /* ---- 心跳输出：独立于输入，每 1 ms 翻转（1 kHz 方波边沿） ---- */
    s_hb_out_level = !s_hb_out_level;
    bsp_hb_out_set(s_hb_out_level);

    /* ---- 装帧：每 20 ms 插一帧波形 ---- */
    bsp_data_ready_set(false);

    if ((now_ms % APP_WAVE_PERIOD_MS) == 0u) {
        build_wave();
    } else {
        build_telemetry();
    }
    bsp_spi2_slave_set_frame(s_tx, FRAME_LEN);
    bsp_data_ready_set(true);
}

/* ------------------------------------------------------------------ */
/* 命令解析                                                            */
/* ------------------------------------------------------------------ */
static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static int16_t get_i16(const uint8_t *p)
{
    return (int16_t)get_u16(p);
}

static int32_t get_i32(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                     ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static bool frame_ok(const uint8_t *b, uint32_t n, uint8_t *plen)
{
    if (n < 5u || b[0] != APP_FRAME_SYNC_RX) {
        return false;
    }
    uint8_t len = b[2];
    if ((uint32_t)len + 5u != n) {
        return false;
    }
    uint16_t crc = crc16_ccitt(b, 3u + len);
    uint16_t got = get_u16(&b[3u + len]);
    *plen = len;
    return crc == got;
}

static void uart_kv(const char *name, int32_t v)
{
    bsp_uart_write(name);
    bsp_uart_write_u32((uint32_t)v);
    bsp_uart_write("\r\n");
}

static void handle_command(const uint8_t *b, uint8_t len)
{
    const uint8_t *p = &b[3];

    switch (b[1]) {
    case APP_CMD_SET_VOLTAGE:
        if (len >= 2u) {
            app_control_set_voltage(get_u16(p));
        }
        break;

    case APP_CMD_SET_CURRENT:
        if (len >= 2u) {
            app_control_set_current(get_u16(p));
        }
        break;

    case APP_CMD_OUTPUT_ON:
        /* 电压和电流上限都必须先设定：0 A 上限会让输出永远无法建立 */
        if (g_status.user_v_mv > 0u && g_status.user_i_ma > 0u) {
            app_control_request_output(true);
        }
        break;

    case APP_CMD_OUTPUT_OFF:
        app_control_request_output(false);
        break;

    case APP_CMD_SCREEN_STATE:
        if (len >= 1u) {
            g_status.screen_on = (p[0] != 0u);
        }
        break;

    case APP_CMD_CLEAR_FAULT:
        app_control_clear_faults();
        break;

    case APP_CMD_CAL_SET_K:
        if (len >= 5u) {
            int32_t v = get_i32(&p[1]);
            if (app_signal_set_k(p[0], v) == 0) {
                g_status.cal_ver = app_signal_version();
            }
        }
        break;

    case APP_CMD_CAL_SET_VPOINT:
        if (len >= 5u) {
            if (app_signal_set_vpoint(p[0], get_u16(&p[1]), get_i16(&p[3])) == 0) {
                g_status.cal_ver = app_signal_version();
            }
        }
        break;

    case APP_CMD_CAL_SET_IPOINT:
        if (len >= 5u) {
            if (app_signal_set_ipoint(p[0], get_u16(&p[1]), get_i16(&p[3])) == 0) {
                g_status.cal_ver = app_signal_version();
            }
        }
        break;

    case APP_CMD_CAL_SAVE:
        if (app_signal_save() == 0) {
            g_status.cal_ver = app_signal_version();
            bsp_uart_write("CAL saved\r\n");
        } else {
            bsp_uart_write("CAL save FAILED\r\n");
        }
        break;

    case APP_CMD_CAL_DEFAULTS:
        app_signal_defaults();
        g_status.cal_ver = app_signal_version();
        bsp_uart_write("CAL defaults\r\n");
        break;

    case APP_CMD_CAL_GET_K:
        if (len >= 1u) {
            int32_t v = 0;
            if (app_signal_get_k(p[0], &v) == 0) {
                uart_kv("K=", v);
            }
        }
        break;

    default:
        break;
    }
}

void app_comms_poll(void)
{
    uint8_t  buf[32];
    uint32_t n = 0u;
    uint8_t  len = 0u;

    /* 每轮最多处理 4 条，避免慢环被命令饿死 */
    for (uint32_t i = 0; i < 4u; i++) {
        if (!bsp_i2c_take_command(buf, sizeof(buf), &n)) {
            break;
        }
        if (frame_ok(buf, n, &len)) {
            handle_command(buf, len);
        } else {
            bsp_uart_write("CMD CRC/len error\r\n");
        }
    }

    bsp_spi2_slave_poll();
}
