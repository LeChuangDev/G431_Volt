/**
 * @file  app_signal.c —— 采样换算 + 五点校准 + DAC 码 + Flash 存取
 *
 * 设计：
 *  - 快环每 100 µs 要调用 4 次换算，全部用整数（Q16 斜率 + 分段线性）。
 *  - 五点校准有效时优先用分段线性；否则用"斜率+截距"。
 *  - NTC 换算需要 logf，只在 100 ms 慢环调用。
 *  - 校准参数整块存最后一页 Flash，带 magic/version/CRC32，掉电不丢。
 */
#include "app_config.h"
#include "app_types.h"
#include "app_signal.h"
#include "bsp.h"
#include <math.h>
#include <string.h>

#define CAL_MAGIC      0x4C564C54u      /* "LVLT" */
#define CAL_VERSION    1u

typedef struct __attribute__((aligned(8))) {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t crc;

    int32_t  v_gain_q16;        /* K1 */
    int32_t  v_off_mv;          /* K2 */
    int32_t  i_gain_q16;        /* K3 */
    int32_t  i_off_ma;          /* K4 */
    int32_t  dac_tempco_x10;    /* K5：LSB / °C × 10 */
    int32_t  ntc_off_x10;       /* K6 的偏置（0.1 °C） */
    uint16_t ntc_r25;           /* K7 */
    uint16_t ntc_beta;          /* K6 */

    uint16_t v_cnt[5];
    int16_t  v_mv[5];
    uint16_t i_cnt[5];
    int16_t  i_ma[5];

    uint8_t  v_pt_valid;
    uint8_t  i_pt_valid;
    uint8_t  reserved[6];
} app_cal_t;

_Static_assert(sizeof(app_cal_t) % 8u == 0u, "cal struct must be 8-byte aligned");

static app_cal_t s_cal;
static uint8_t    s_version;

/* ------------------------------------------------------------------ */
/* 工具                                                               */
/* ------------------------------------------------------------------ */
static uint32_t crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint32_t b = 0; b < 8u; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static bool points_ok(const uint16_t *cnt)
{
    for (uint32_t i = 0; i < 4u; i++) {
        if (cnt[i + 1u] <= cnt[i]) {
            return false;
        }
    }
    return true;
}

static uint32_t piecewise(uint16_t raw, const uint16_t *cnt, const int16_t *val)
{
    if (raw <= cnt[0]) {
        return (val[0] > 0) ? (uint32_t)val[0] : 0u;
    }
    for (uint32_t i = 0; i < 4u; i++) {
        if (raw <= cnt[i + 1u]) {
            uint32_t span = (uint32_t)cnt[i + 1u] - cnt[i];
            int32_t  dy   = (int32_t)val[i + 1u] - val[i];
            int32_t  dx   = (int32_t)raw - cnt[i];
            int32_t  y    = (int32_t)val[i] + (dx * dy) / (int32_t)span;
            return (y > 0) ? (uint32_t)y : 0u;
        }
    }
    return (val[4] > 0) ? (uint32_t)val[4] : 0u;
}

static uint32_t apply_affine(uint32_t x, int32_t gain_q16, int32_t offset)
{
    int32_t y = (int32_t)(((int64_t)x * gain_q16) >> 16);
    y += offset;
    return (y > 0) ? (uint32_t)y : 0u;
}

/* ------------------------------------------------------------------ */
/* 初始化 / 默认值                                                    */
/* ------------------------------------------------------------------ */
void app_signal_defaults(void)
{
    memset(&s_cal, 0, sizeof(s_cal));
    s_cal.magic = CAL_MAGIC;
    s_cal.version = CAL_VERSION;
    s_cal.size = (uint16_t)sizeof(s_cal);
    s_cal.v_gain_q16 = 65536;
    s_cal.i_gain_q16 = 65536;
    s_cal.ntc_r25 = BSP_NTC_R25_OHM;
    s_cal.ntc_beta = BSP_NTC_BETA;
    s_cal.v_pt_valid = 0u;
    s_cal.i_pt_valid = 0u;
    s_version = 0u;
}

static bool cal_valid(const app_cal_t *c)
{
    if (c->magic != CAL_MAGIC || c->version != CAL_VERSION ||
        c->size != sizeof(app_cal_t)) {
        return false;
    }
    app_cal_t tmp = *c;
    tmp.crc = 0u;
    return crc32((const uint8_t *)&tmp, sizeof(tmp)) == c->crc;
}

void app_signal_init(void)
{
    const app_cal_t *flash = (const app_cal_t *)BSP_FLASH_CFG_ADDR;

    if (cal_valid(flash)) {
        s_cal = *flash;
        s_version = (uint8_t)(s_cal.crc & 0xFFu);
    } else {
        app_signal_defaults();
    }
}

uint8_t app_signal_version(void)
{
    return s_version;
}

/* ------------------------------------------------------------------ */
/* 工程量换算                                                         */
/* ------------------------------------------------------------------ */
uint32_t app_signal_vout_mv(uint16_t raw)
{
    if (s_cal.v_pt_valid != 0u && points_ok(s_cal.v_cnt)) {
        return piecewise(raw, s_cal.v_cnt, s_cal.v_mv);
    }
    return apply_affine(bsp_vout_mv(raw), s_cal.v_gain_q16, s_cal.v_off_mv);
}

uint32_t app_signal_iout_ma(uint16_t raw)
{
    if (s_cal.i_pt_valid != 0u && points_ok(s_cal.i_cnt)) {
        return piecewise(raw, s_cal.i_cnt, s_cal.i_ma);
    }
    return apply_affine(bsp_iout_ma(raw), s_cal.i_gain_q16, s_cal.i_off_ma);
}

uint32_t app_signal_vin_mv(uint16_t raw)
{
    return apply_affine(bsp_vin_mv(raw), s_cal.v_gain_q16, s_cal.v_off_mv);
}

uint32_t app_signal_iin_ma(uint16_t raw)
{
    return apply_affine(bsp_iin_ma(raw), s_cal.i_gain_q16, s_cal.i_off_ma);
}

uint16_t app_signal_ntc_beta(void)
{
    return s_cal.ntc_beta;
}

uint16_t app_signal_ntc_r25(void)
{
    return s_cal.ntc_r25;
}

bool app_signal_ntc_x10(uint16_t raw, int16_t *t_x10)
{
    if (raw == 0u || raw >= 4095u) {
        return false;
    }
    if (raw > BSP_NTC_OPEN_RAW || raw < BSP_NTC_SHORT_RAW) {
        return false;
    }

    float r_ntc = (float)BSP_NTC_R_FIXED_OHM * (float)raw / (4095.0f - (float)raw);
    float r25   = (float)((s_cal.ntc_r25 != 0u) ? s_cal.ntc_r25 : BSP_NTC_R25_OHM);
    float beta  = (float)((s_cal.ntc_beta != 0u) ? s_cal.ntc_beta : BSP_NTC_BETA);

    float inv_t = (1.0f / 298.15f) + (logf(r_ntc / r25) / beta);
    float t_c   = (1.0f / inv_t) - 273.15f;

    int32_t t = (int32_t)(t_c * 10.0f) + s_cal.ntc_off_x10;
    if (t < -400)  t = -400;
    if (t > 1500)  t = 1500;
    *t_x10 = (int16_t)t;
    return true;
}

/* ------------------------------------------------------------------ */
/* 设定值 → DAC 码                                                    */
/* ------------------------------------------------------------------ */
static int32_t dac_temp_trim(void)
{
    /* trim = (T_case − 25 °C) × tempco(LSB/°C)
       存储单位：0.1 °C 与 LSB/°C × 10 → 除以 100 */
    return ((int32_t)g_status.t_case_x10 - 250) * s_cal.dac_tempco_x10 / 100;
}

uint16_t app_signal_vset_code(uint16_t vout_mv)
{
    if (vout_mv > 45000u) {
        vout_mv = 45000u;
    }
    /* VOUT = 45.000 − 10 × VDAC → VDAC_mV = (45000 − VOUT_mV) / 10 */
    int32_t vdac_mv = ((int32_t)BSP_VOUT_EQ_OFFSET_MV - (int32_t)vout_mv) / 10;
    if (vdac_mv < 0) {
        vdac_mv = 0;
    }
    int32_t code = (vdac_mv * 65535) / (int32_t)BSP_VDAC_FULL_MV;
    code += dac_temp_trim();
    if (code < 0) {
        code = 0;
    }
    if (code > 65535) {
        code = 65535;
    }
    return (uint16_t)code;
}

uint16_t app_signal_iset_code(uint16_t i_ma)
{
    if (i_ma > BSP_ISET_FULL_MA) {
        i_ma = BSP_ISET_FULL_MA;
    }
    int32_t mv = ((int32_t)i_ma * (int32_t)BSP_ISET_FULL_MV) / (int32_t)BSP_ISET_FULL_MA;
    int32_t code = (mv * 65535) / (int32_t)BSP_VDAC_FULL_MV;
    if (code < 0) {
        code = 0;
    }
    if (code > 65535) {
        code = 65535;
    }
    return (uint16_t)code;
}

/* ------------------------------------------------------------------ */
/* 校准参数                                                           */
/* ------------------------------------------------------------------ */
int app_signal_set_k(uint8_t k, int32_t value)
{
    switch (k) {
    case APP_K_V_GAIN:      s_cal.v_gain_q16 = value; break;
    case APP_K_V_OFFSET:    s_cal.v_off_mv = value; break;
    case APP_K_I_GAIN:      s_cal.i_gain_q16 = value; break;
    case APP_K_I_OFFSET:    s_cal.i_off_ma = value; break;
    case APP_K_DAC_TEMPCO:  s_cal.dac_tempco_x10 = value; break;
    case APP_K_NTC_BETA:    s_cal.ntc_beta = (uint16_t)value; break;
    case APP_K_NTC_R25:     s_cal.ntc_r25 = (uint16_t)value; break;
    default:                return -1;
    }
    return 0;
}

int app_signal_get_k(uint8_t k, int32_t *value)
{
    if (value == 0) {
        return -1;
    }
    switch (k) {
    case APP_K_V_GAIN:      *value = s_cal.v_gain_q16; break;
    case APP_K_V_OFFSET:    *value = s_cal.v_off_mv; break;
    case APP_K_I_GAIN:      *value = s_cal.i_gain_q16; break;
    case APP_K_I_OFFSET:    *value = s_cal.i_off_ma; break;
    case APP_K_DAC_TEMPCO:  *value = s_cal.dac_tempco_x10; break;
    case APP_K_NTC_BETA:    *value = (int32_t)s_cal.ntc_beta; break;
    case APP_K_NTC_R25:     *value = (int32_t)s_cal.ntc_r25; break;
    default:                return -1;
    }
    return 0;
}

int app_signal_set_vpoint(uint8_t idx, uint16_t raw, int16_t mv)
{
    if (idx >= 5u) {
        return -1;
    }
    s_cal.v_cnt[idx] = raw;
    s_cal.v_mv[idx] = mv;
    s_cal.v_pt_valid = points_ok(s_cal.v_cnt) ? 1u : 0u;
    return 0;
}

int app_signal_set_ipoint(uint8_t idx, uint16_t raw, int16_t ma)
{
    if (idx >= 5u) {
        return -1;
    }
    s_cal.i_cnt[idx] = raw;
    s_cal.i_ma[idx] = ma;
    s_cal.i_pt_valid = points_ok(s_cal.i_cnt) ? 1u : 0u;
    return 0;
}

int app_signal_save(void)
{
    app_cal_t tmp = s_cal;
    tmp.magic = CAL_MAGIC;
    tmp.version = CAL_VERSION;
    tmp.size = (uint16_t)sizeof(tmp);
    tmp.crc = 0u;
    tmp.crc = crc32((const uint8_t *)&tmp, sizeof(tmp));

    int rc = bsp_flash_erase_cfg_page();
    if (rc == 0) {
        rc = bsp_flash_program_cfg(0u, &tmp, sizeof(tmp));
    }
    if (rc == 0) {
        s_cal = tmp;
        s_version = (uint8_t)(tmp.crc & 0xFFu);
    }
    return rc;
}
