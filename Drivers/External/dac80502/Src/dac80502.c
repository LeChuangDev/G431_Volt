/**
 * @file  dac80502.c —— DAC80502 协议层（无引脚、无 SPI 实例依赖）
 */
#include "dac80502.h"

#define DAC80502_REG_CONFIG   0x04u
#define DAC80502_REG_GAIN     0x05u
#define DAC80502_REG_DAC0     0x08u
#define DAC80502_REG_DAC1     0x09u

#define DAC80502_CFG_REF_DIV  (1u << 8)   /* 1 = 内部基准二分频 */
#define DAC80502_GAIN_X2      (1u << 8)   /* 1 = 输出增益 ×2   */

static dac80502_xfer_fn s_xfer;

void dac80502_bind(dac80502_xfer_fn fn)
{
    s_xfer = fn;
}

int dac80502_write_reg(uint8_t reg, uint16_t value)
{
    if (s_xfer == 0) {
        return -1;
    }
    const uint8_t frame[3] = {
        reg,
        (uint8_t)(value >> 8),
        (uint8_t)(value & 0xFFu)
    };
    return s_xfer(frame, sizeof(frame));
}

int dac80502_set_channel(uint8_t ch, uint16_t code)
{
    return dac80502_write_reg((ch == 0u) ? DAC80502_REG_DAC0
                                         : DAC80502_REG_DAC1, code);
}

int dac80502_set_channels(uint16_t ch0, uint16_t ch1)
{
    int rc = dac80502_set_channel(0u, ch0);
    if (rc != 0) {
        return rc;
    }
    return dac80502_set_channel(1u, ch1);
}

int dac80502_init(void)
{
    if (s_xfer == 0) {
        return -1;
    }

    /* 内部 2.5 V 基准不做分频，缓冲器增益 ×2 → 0..5.000 V 满量程 */
    int rc = dac80502_write_reg(DAC80502_REG_CONFIG, 0u);
    if (rc != 0) {
        return rc;
    }
    rc = dac80502_write_reg(DAC80502_REG_GAIN, DAC80502_GAIN_X2);
    if (rc != 0) {
        return rc;
    }
    return dac80502_set_channels(0u, 0u);
}
