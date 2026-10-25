/**
 * @file  ch224q.c —— CH224Q 协议层
 */
#include "ch224q.h"

#define CH224Q_REG_STATUS    0x00u
#define CH224Q_REG_VBUS_H    0x01u
#define CH224Q_REG_VBUS_L    0x02u
#define CH224Q_REG_VOLTAGE   0x09u
#define CH224Q_REG_CURRENT   0x0Au

static ch224q_read_fn  s_rd;
static ch224q_write_fn s_wr;

/* 电压档位 → 索引 */
static const struct {
    uint8_t volts;
    uint8_t idx;
} s_v_table[] = {
    { 5u, 0u }, { 9u, 1u }, { 12u, 2u },
    { 15u, 3u }, { 20u, 4u }, { 28u, 5u },
};

/* 电流档位（mA）→ 索引；按"不超过请求值"向下取档。
   ★ 具体档位以 CH224Q 手册为准，这里给出常见 PD 电流档 ★ */
static const struct {
    uint32_t ma;
    uint8_t idx;
} s_i_table[] = {
    { 1500u, 0u }, { 2000u, 1u }, { 2500u, 2u },
    { 3000u, 3u }, { 3250u, 4u }, { 4300u, 5u },
};

void ch224q_bind(ch224q_read_fn rd, ch224q_write_fn wr)
{
    s_rd = rd;
    s_wr = wr;
}

int ch224q_set_voltage(uint8_t volts)
{
    if (s_wr == 0) {
        return -1;
    }
    for (uint32_t i = 0; i < sizeof(s_v_table) / sizeof(s_v_table[0]); i++) {
        if (s_v_table[i].volts == volts) {
            return s_wr(CH224Q_REG_VOLTAGE, s_v_table[i].idx);
        }
    }
    return -2;
}

int ch224q_set_current_ma(uint32_t ma)
{
    if (s_wr == 0) {
        return -1;
    }
    uint8_t best = 0u;
    for (uint32_t i = 0; i < sizeof(s_i_table) / sizeof(s_i_table[0]); i++) {
        if (s_i_table[i].ma <= ma) {
            best = s_i_table[i].idx;
        }
    }
    return s_wr(CH224Q_REG_CURRENT, best);
}

int ch224q_read_status(uint8_t *status)
{
    if (s_rd == 0 || status == 0) {
        return -1;
    }
    return s_rd(CH224Q_REG_STATUS, status, 1u);
}

int ch224q_read_vbus_raw(uint16_t *raw)
{
    if (s_rd == 0 || raw == 0) {
        return -1;
    }
    uint8_t b[2];
    int rc = s_rd(CH224Q_REG_VBUS_H, b, 2u);
    if (rc != 0) {
        return rc;
    }
    *raw = (uint16_t)(((uint16_t)b[0] << 8) | b[1]);
    return 0;
}
