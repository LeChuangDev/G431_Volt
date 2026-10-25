/**
 * @file  dac80502.h —— TI DAC80502（16-bit 双通道 SPI DAC）协议层
 *
 * 只负责"这个芯片的寄存器怎么读写"，不关心 CS 接哪个脚、SPI 用哪一路。
 * 板级绑定在 BSP/Src/bsp_vdac.c 完成（通过 dac80502_bind 注入传输函数）。
 *
 * 帧格式：24 bit = 8 bit 寄存器地址 + 16 bit 数据，MSB first；
 *         CS 下降沿开始、上升沿锁存，SPI mode 1（CPOL=0, CPHA=1）。
 *
 * ★ 寄存器地址与位定义请对照所用批次的数据手册再核对一次 ★
 *   下面按 DAC8050x 系列通用映射给出：
 *     0x04 CONFIG   bit8 = REF-DIV（0 = 2.5 V，1 = 1.25 V）
 *     0x05 GAIN     bit8 = BUFF-GAIN（0 = ×1，1 = ×2）
 *     0x08 DAC0
 *     0x09 DAC1
 */
#ifndef __DAC80502_H
#define __DAC80502_H

#include <stdint.h>

typedef int (*dac80502_xfer_fn)(const uint8_t *tx, uint32_t len);

void dac80502_bind(dac80502_xfer_fn fn);

int dac80502_init(void);                       /* CONFIG/GAIN + 两通道清零 */
int dac80502_set_channel(uint8_t ch, uint16_t code);
int dac80502_set_channels(uint16_t ch0, uint16_t ch1);
int dac80502_write_reg(uint8_t reg, uint16_t value);

#endif /* __DAC80502_H */
