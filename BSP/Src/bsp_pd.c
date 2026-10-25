/**
 * @file  bsp_pd.c —— CH224Q 的板级绑定（I2C1 @ 0x23）
 *
 * 说明：PD 协商是否成功的最终判据用 ADC 测到的 VIN，而不是 CH224Q 的
 *       状态寄存器——这样即使芯片寄存器编码与手册有出入，控制环仍然安全。
 *       VBUS 寄存器读数只作为日志诊断使用。
 */
#include "bsp.h"
#include "ch224q.h"

#define CH224Q_ADDR7   0x23u

static int pd_read(uint8_t reg, uint8_t *buf, uint32_t len)
{
    return bsp_i2c_master_write_read(CH224Q_ADDR7, &reg, 1u, buf, len);
}

static int pd_write(uint8_t reg, uint8_t value)
{
    uint8_t d[2] = { reg, value };
    return bsp_i2c_master_write(CH224Q_ADDR7, d, 2u);
}

void bsp_pd_init(void)
{
    ch224q_bind(pd_read, pd_write);
}

int bsp_pd_set_voltage(uint8_t volts)
{
    return ch224q_set_voltage(volts);
}

int bsp_pd_set_current(uint32_t ma)
{
    return ch224q_set_current_ma(ma);
}

int bsp_pd_read_vbus_mv(uint16_t *mv)
{
    uint16_t raw;
    int rc = ch224q_read_vbus_raw(&raw);
    if (rc != 0) {
        return rc;
    }
    /* 单位按 100 mV 处理；仅用于诊断，不参与控制 */
    *mv = (uint16_t)(raw * 100u);
    return 0;
}
