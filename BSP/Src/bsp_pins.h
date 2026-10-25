/**
 * @file  bsp_pins.h —— 引脚到 GPIOx / 复用功能的私有映射（只给 BSP 源文件用）
 * 与 bsp_board.h 的引脚总览一一对应；换板子时两个文件一起改。
 */
#ifndef __BSP_PINS_H
#define __BSP_PINS_H

#include "stm32g4xx.h"

/* ---- 复用功能编号（RM0440 AF 表）---- */
#define BSP_AF4_I2C     4u
#define BSP_AF5_SPI     5u      /* SPI1 / SPI2 都是 AF5 */
#define BSP_AF7_USART   7u

/* ---- 输出 ---- */
#define PIN_OUT_EN_GPIO     GPIOB
#define PIN_OUT_EN_NUM      10u
#define PIN_SINK_EN_GPIO    GPIOB
#define PIN_SINK_EN_NUM     11u
#define PIN_HB_OUT_GPIO     GPIOC
#define PIN_HB_OUT_NUM      6u
#define PIN_DATA_RDY_GPIO   GPIOC
#define PIN_DATA_RDY_NUM    8u
#define PIN_VDAC_CS_GPIO    GPIOA
#define PIN_VDAC_CS_NUM     8u

/* ---- 输入 ---- */
#define PIN_NFAULT_GPIO     GPIOB
#define PIN_NFAULT_NUM      12u
#define PIN_HB_IN_GPIO      GPIOC
#define PIN_HB_IN_NUM       7u
#define PIN_PROT_TRIP_GPIO  GPIOA
#define PIN_PROT_TRIP_NUM   15u

/* ---- 模拟 ---- */
#define PIN_VOUT_GPIO       GPIOA
#define PIN_VOUT_NUM        0u
#define PIN_IOUT_GPIO       GPIOA
#define PIN_IOUT_NUM        1u
#define PIN_VIN_GPIO        GPIOA
#define PIN_VIN_NUM         2u
#define PIN_IIN_GPIO        GPIOA
#define PIN_IIN_NUM         3u
#define PIN_ICTRL_GPIO      GPIOA
#define PIN_ICTRL_NUM       4u
#define PIN_NTC_GPIO        GPIOA
#define PIN_NTC_NUM         6u
#define PIN_OCP_GPIO        GPIOA
#define PIN_OCP_NUM         7u
#define PIN_OVP_GPIO        GPIOB
#define PIN_OVP_NUM         0u

/* ---- SPI1（外部 DAC80502）---- */
#define PIN_SPI1_SCK_GPIO   GPIOB
#define PIN_SPI1_SCK_NUM    3u
#define PIN_SPI1_MISO_GPIO  GPIOB
#define PIN_SPI1_MISO_NUM   4u
#define PIN_SPI1_MOSI_GPIO  GPIOB
#define PIN_SPI1_MOSI_NUM   5u

/* ---- SPI2（ESP32 遥测，从机）---- */
#define PIN_SPI2_SCK_GPIO   GPIOB
#define PIN_SPI2_SCK_NUM    13u
#define PIN_SPI2_MISO_GPIO  GPIOB
#define PIN_SPI2_MISO_NUM   14u
#define PIN_SPI2_MOSI_GPIO  GPIOB
#define PIN_SPI2_MOSI_NUM   15u
#define PIN_SPI2_NSS_GPIO   GPIOB
#define PIN_SPI2_NSS_NUM    9u

/* ---- I2C1（CH224Q + ESP32 命令）---- */
#define PIN_I2C1_SCL_GPIO   GPIOB
#define PIN_I2C1_SCL_NUM    6u
#define PIN_I2C1_SDA_GPIO   GPIOB
#define PIN_I2C1_SDA_NUM    7u

/* ---- USART1 ---- */
#define PIN_UART_TX_GPIO    GPIOA
#define PIN_UART_TX_NUM     9u
#define PIN_UART_RX_GPIO    GPIOA
#define PIN_UART_RX_NUM     10u

#endif /* __BSP_PINS_H */
