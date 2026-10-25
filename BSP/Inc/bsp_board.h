/**
 * @file  bsp_board.h —— LeVolt 电源板：引脚索引、模拟前端标定常数、Flash 布局
 *
 * 本文件是【板级唯一真源】，App 层可以放心 include（不依赖任何芯片头）。
 * 换板子时只改本文件 + BSP/Src/bsp_pins.h。
 *
 * ── 引脚总览（STM32G431CB / LQFP48）────────────────────────────────────
 *  模拟输入（ADC 注入组，TIM6 100 µs 触发）
 *    PA0  VOUT      ADC1_IN1
 *    PA1  IOUT      ADC1_IN2
 *    PA2  VIN       ADC1_IN3
 *    PA3  IIN       ADC1_IN4
 *    PA6  NTC       ADC2_IN3   （10 kΩ NTC 下拉 + 10 kΩ 上拉到 3.3 V）
 *    PA7  OCP_SENSE ADC2_IN4
 *    PB0  OVP_SENSE ADC2_IN15
 *  设定输出
 *    PA4  ICTRL     DAC1_OUT1（内部 12-bit，0..3.3 V）
 *    PB3  SPI1_SCK  }              外部 16-bit DAC80502
 *    PB4  SPI1_MISO } AF5          双通道：CH0 = VSET，CH1 = ISET
 *    PB5  SPI1_MOSI }
 *    PA8  VDAC_CS   GPIO 推挽，低有效
 *  通信
 *    PB6  I2C1_SCL  AF4  ── CH224Q（主）+ ESP32 命令（从，0x24）
 *    PB7  I2C1_SDA  AF4
 *    PB13 SPI2_SCK  }              与 ESP32 的遥测链路（G431 = 从机）
 *    PB14 SPI2_MISO }
 *    PB15 SPI2_MOSI }
 *    PB9  SPI2_NSS  AF5
 *    PA9  UART1_TX  AF7            调试日志
 *    PA10 UART1_RX  AF7
 *  控制 / 状态
 *    PB10 OUT_EN    上电默认高 = 禁止输出；拉低 = 允许源出
 *    PB11 SINK_EN   高 = 打开 47 Ω 有源下拉
 *    PA15 PROT_TRIP 输入上拉，下降沿 EXTI（硬件比较器故障）
 *    PB12 nFAULT    输入上拉，低有效
 *    PC6  HB_OUT    心跳输出（1 kHz 翻转）
 *    PC7  HB_IN     心跳输入（来自 ESP32）
 *    PC8  DATA_RDY  遥测帧就绪（给 ESP32 的中断）
 *
 * ── 模拟前端换算（按实际硬件修改！）────────────────────────────────────
 *   电压分压 15:1，0..45 V → 0..3.0 V
 *   输出电流检测 0.825 V/A，0..4 A → 0..3.3 V
 *   输入电流检测 0.750 V/A，0..4.4 A → 0..3.3 V
 *   所有结果已在 BSP 内换算成 mV / mA，App 只需再叠加校准系数。
 */
#ifndef __BSP_BOARD_H
#define __BSP_BOARD_H

#include <stdint.h>

/* ---- ADC 基准与满量程（12-bit，VDDA = 3.3 V）---- */
#define BSP_ADC_VREF_MV        3300u
#define BSP_ADC_FULL_SCALE     4095u

/* ---- 模拟前端比例 ---- */
#define BSP_VSENSE_DIV_X100    1500u    /* 15.00 : 1（VOUT / VIN 共用） */
#define BSP_IOUT_MV_PER_A      825u     /* 0.825 V/A */
#define BSP_IIN_MV_PER_A       750u     /* 0.750 V/A */

/* ---- NTC（PA6）：NTC 接地 + 10 kΩ 上拉 ---- */
#define BSP_NTC_R25_OHM        10000u
#define BSP_NTC_BETA           3950u
#define BSP_NTC_R_FIXED_OHM    10000u
#define BSP_NTC_OPEN_RAW       4070u    /* > 此值认为 NTC 断路 */
#define BSP_NTC_SHORT_RAW      20u      /* < 此值认为 NTC 短路 */

/* ---- 内部 DAC（ICTRL）满量程：0..3300 mV ↔ 0..4.5 A ---- */
#define BSP_ICTRL_FULL_MV      3300u
#define BSP_ICTRL_FULL_MA      4500u

/* ---- 外部 DAC80502 满量程：内部 2.5 V × 增益 2 = 5.000 V ---- */
#define BSP_VDAC_FULL_MV       5000u

/* ---- 电压设定注入公式 VOUT = 45.000 − 10.000 × VDAC ----
 * 与 5 V 满量程的 DAC80502 配合：
 *   VOUT = 0 V   → VDAC = 4.500 V → code 58982
 *   VOUT = 36 V  → VDAC = 0.900 V → code 11796
 *   VOUT = 45 V  → VDAC = 0.000 V → code 0（DAC 上电默认值！）
 * ★ 因此 DAC 复位默认码 0 对应 45 V 设定，OUT_EN 必须在上电时保持禁止，
 *   固件写完 0 V 码并确认后才能拉低。硬件需给 PB10 加上拉。
 */
#define BSP_VOUT_EQ_OFFSET_MV  45000   /* 45.000 V */
#define BSP_VOUT_EQ_GAIN_X10   100u    /* 10.000（×10） */

/* ---- 输出电流设定（DAC80502 CH1）：0 mV ↔ 0 A，3300 mV ↔ 3.15 A ---- */
#define BSP_ISET_FULL_MV       3300u
#define BSP_ISET_FULL_MA       3150u

/* ---- Flash 校准参数区：最后 1 页（2 KB）---- */
#define BSP_FLASH_BASE         0x08000000u
#define BSP_FLASH_CFG_PAGE     63u
#define BSP_FLASH_PAGE_SIZE    2048u
#define BSP_FLASH_CFG_ADDR     (BSP_FLASH_BASE + BSP_FLASH_CFG_PAGE * BSP_FLASH_PAGE_SIZE)

/* ---- 由比例推导的定点换算（Q16：结果 = raw * Q >> 16）---- */
#define BSP_SCALE_Q16(num, den) \
    ((uint32_t)(((uint64_t)(num) * 65536u + ((den) / 2u)) / (den)))

#define BSP_VSENSE_Q16  BSP_SCALE_Q16(BSP_ADC_VREF_MV * BSP_VSENSE_DIV_X100, \
                                      BSP_ADC_FULL_SCALE * 100u)
#define BSP_IOUT_Q16    BSP_SCALE_Q16(BSP_ADC_VREF_MV * 1000u, \
                                      BSP_ADC_FULL_SCALE * BSP_IOUT_MV_PER_A)
#define BSP_IIN_Q16     BSP_SCALE_Q16(BSP_ADC_VREF_MV * 1000u, \
                                      BSP_ADC_FULL_SCALE * BSP_IIN_MV_PER_A)
#define BSP_NTC_MV_Q16  BSP_SCALE_Q16(BSP_ADC_VREF_MV, BSP_ADC_FULL_SCALE)

/* ---- 引脚编号（实际 GPIOx 映射在 BSP/Src/bsp_pins.h）---- */
#define BSP_PIN_OUT_EN      10u     /* PB10 */
#define BSP_PIN_SINK_EN     11u     /* PB11 */
#define BSP_PIN_NFAULT      12u     /* PB12 */
#define BSP_PIN_PROT_TRIP   15u     /* PA15 */
#define BSP_PIN_HB_OUT      6u      /* PC6  */
#define BSP_PIN_HB_IN       7u      /* PC7  */
#define BSP_PIN_DATA_READY  8u      /* PC8  */

/* ---- 与 ESP32 的 SPI 帧固定长度（字节）---- */
#define BSP_SPI_FRAME_LEN   32u

/* ---- I2C 命令通道 ---- */
#define BSP_I2C_CMD_ADDR    0x24u   /* G431 的 7-bit 从机地址 */
#define BSP_I2C_CMD_MAX     24u
#define BSP_I2C_CMD_SLOTS   4u

#endif /* __BSP_BOARD_H */
