/**
 * @file  bsp_bitband.h —— Cortex-M3/M4 位带(bit-band)操作
 *
 *  位带原理：
 *    ARM 把两块 1MB 地址空间各映射成一块 32MB 的"别名区"，
 *    别名区里 1 个字(4字节) = 原地址的 1 个位。
 *    对别名区某个字写 1/0，硬件原子地完成"读-改-写"，只动那一位。
 *
 *  两个可位带的区域（Cortex-M4 只有这两块）：
 *    1. SRAM       区: 0x2000_0000 ~ 0x200F_FFFF（1MB）
 *       别名区:        0x2200_0000 起（每 1 位占 4 字节）
 *    2. 外设寄存器区: 0x4000_0000 ~ 0x400F_FFFF（1MB，GPIO/USART/SPI 等全在内）
 *       别名区:        0x4200_0000 起
 *
 *  换算公式:
 *    别名地址 = 别名区基址 + (原地址 - 原区域基址) * 32 + 位号 * 4
 *
 *  ★ 重要限制：不在上面两个 1MB 范围内的地址【不能】位带！
 *    例如 USB OTG FS(0x5000_0000) 挂在 AHB2 上，位带不到它。
 *
 *  ★ STM32G4 特别注意 ★
 *    G4 的 GPIO 寄存器基址是 0x4800_0000，落在位带区
 *    (0x4000_0000~0x400F_FFFF) 之外，所以【不能用位带操作 GPIO】。
 *    本模板的 BSP LED 已改用 GPIOx->BSRR；本文件仅保留给
 *    位于 0x4000_0000~0x400F_FFFF 的外设（如部分 TIM）和 SRAM 使用。
 */
#ifndef __BSP_BITBAND_H
#define __BSP_BITBAND_H

// 由外部项目头文件（如 CMSIS/stm32g4xx.h）已提供 uint32_t，无需重复包含

/* ---- 底层：算别名地址 ---- */
#define BITBAND_SRAM_ADDR(addr, bit)  \
    (0x22000000UL + (((uint32_t)(addr) - 0x20000000UL) * 32UL) + ((uint32_t)(bit) * 4UL))

#define BITBAND_PERI_ADDR(addr, bit)  \
    (0x42000000UL + (((uint32_t)(addr) - 0x40000000UL) * 32UL) + ((uint32_t)(bit) * 4UL))

/* ---- 顶层：直接读写某个位 ---- */
/* 用法: BITBAND_SRAM(&某SRAM变量, 3) = 1;   读同理
         BITBAND_PERI(&GPIOD->ODR, 12) = 0;  原子地只清第 12 位，其余位不受影响 */
#define BITBAND_SRAM(addr, bit)   (*(volatile uint32_t *)BITBAND_SRAM_ADDR((addr), (bit)))
#define BITBAND_PERI(addr, bit)   (*(volatile uint32_t *)BITBAND_PERI_ADDR((addr), (bit)))

/* ---- 常用缩写：直接对寄存器名操作 ---- */
#define BB_REG(reg, bit)  BITBAND_PERI((uint32_t *)&(reg), (bit))
#define BB_VAR(var, bit)  BITBAND_SRAM((uint32_t *)&(var), (bit))

/* =====================================================================
 *  使用示例（对比普通写法）：
 *
 *  // 普通写法：置位/清位（读-改-写，中断可能插在中间破坏其他位）
 *  GPIOD->ODR |=  (1u << 12u);
 *  GPIOD->ODR &= ~(1u << 12u);
 *
 *  // 位带写法：硬件保证原子，中断安全
 *  BB_REG(GPIOD->ODR, 12) = 1;     // 置位
 *  BB_REG(GPIOD->ODR, 12) = 0;     // 清位
 *
 *  // 翻转：读位 -> 取反写回
 *  BB_REG(GPIOD->ODR, 12) = !BB_REG(GPIOD->ODR, 12);
 *
 *  // SRAM 里的标志位（多任务/中断共享标志常用）
 *  static uint32_t g_flags = 0;
 *  BB_VAR(g_flags, 0) = 1;         // 原子地置标志位 0
 *  if (BB_VAR(g_flags, 0)) { ... } // 原子地读标志位 0
 *
 *  // 外设状态位原子操作，如 USART 发送忙标志
 *  BB_REG(USART1->SR, 7) = 1;      // TC 标志清除是写 0 才清，这里仅演示
 * ===================================================================== */

#endif /* __BSP_BITBAND_H */