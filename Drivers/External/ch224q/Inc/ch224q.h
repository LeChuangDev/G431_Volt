/**
 * @file  ch224q.h —— WCH CH224Q USB-PD 受电控制器（I2C 协议层）
 *
 * 只描述寄存器访问与档位映射，不绑定 I2C 实例/引脚；BSP 里注入读写函数。
 *
 * ★ 寄存器地址与档位编码请对照 CH224Q 数据手册核对 ★
 *   本文件按常见映射实现：
 *     0x00 STATUS   只读
 *     0x01 VBUS_H   只读（诊断用）
 *     0x02 VBUS_L
 *     0x09 VOLTAGE  写入档位索引
 *     0x0A CURRENT  写入限流档位索引
 *   电压索引：0=5 V, 1=9 V, 2=12 V, 3=15 V, 4=20 V, 5=28 V
 *   电流索引：见 ch224q.c 的 s_current_table
 *
 * 如果实际芯片的地址/编码不同，只改本驱动和 BSP/PINS，App 层不受影响。
 */
#ifndef __CH224Q_H
#define __CH224Q_H

#include <stdint.h>

typedef int (*ch224q_read_fn)(uint8_t reg, uint8_t *buf, uint32_t len);
typedef int (*ch224q_write_fn)(uint8_t reg, uint8_t value);

void ch224q_bind(ch224q_read_fn rd, ch224q_write_fn wr);

int ch224q_set_voltage(uint8_t volts);          /* 5/9/12/15/20/28 */
int ch224q_set_current_ma(uint32_t ma);         /* 就近向下取档 */
int ch224q_read_status(uint8_t *status);
int ch224q_read_vbus_raw(uint16_t *raw);

#endif /* __CH224Q_H */
