/**
 * @file  bsp_clock.c —— 纯寄存器配置系统时钟 (STM32G431)
 * 目标: HSE 8MHz -> PLL -> SYSCLK 170MHz
 *        AHB=170MHz / APB1=170MHz / APB2=170MHz（G4 三大总线均可到 170MHz）
 * 参考: RM0440 参考手册 "Reset and clock control (RCC)" 章节
 *
 * ★ G4 与 F4 时钟树的关键差异（换芯片时最容易踩的坑）★
 *   1) PLL 结构：G4 是 M 分频 -> N 倍频 -> R 分频（R 还有 P/Q 输出），
 *      不再是 F4 的 M/N/P/Q 四路输出；SYSCLK 固定从 PLLR 出。
 *   2) 170MHz 必须开 Boost 模式：PWR_CR5.R1MODE=0 + PWR_CR1.VOS=Range1，
 *      只设 VOS 不设 R1MODE，最高只能稳在 150MHz。
 *   3) Flash 等待周期：170MHz / Range1 需要 4WS（FLASH_ACR_LATENCY_4WS）。
 *   4) PLL 输入级要求 2.66~16MHz，VCO 要求 96~344MHz：
 *      8MHz / 2 = 4MHz，4MHz × 85 = 340MHz，340MHz / 2 = 170MHz。
 *
 * ★ 与 SystemInit() 的关系 ★
 *   CubeG4 的 SystemInit() 只做 FPU 使能和向量表重定位，不配置 PLL。
 *   本函数仍然不假设当前时钟源，先切回 HSI、关闭 PLL，再完整重配：
 *     1) RM0440 规定 PLLCFGR 只有在 PLL 关闭时才可写；
 *     2) 先降回 HSI 再改分频，避免高频下出现瞬时超频；
 *     3) 所有等待都带超时，HSE/PLL 异常时回退 HSI，不会卡死在 main 里。
 */
#include "bsp.h"
#include "stm32g4xx.h"          /* 全部寄存器定义（CubeG4 CMSIS 器件头） */

/* 等待超时：即使还在 16MHz HSI 上，也足够覆盖晶振起振（典型 1~2ms）。
   超时不是为了精确计时，只是避免晶振缺失/参数错误时永久卡死。 */
#define CLOCK_WAIT_TIMEOUT  500000u

/* PLL 参数（按 8MHz HSE 计算，换晶振只改这里）
   PLLM 字段 = 分频值 - 1：1 表示 ÷2
   PLLN 字段 = 倍频值本身：85
   PLLR 字段：0=÷2, 1=÷4, 2=÷6, 3=÷8 */
#define CLOCK_PLLM_FIELD    1u      /* 8MHz  ÷2  = 4MHz   (PLL 输入) */
#define CLOCK_PLLN_FIELD    85u     /* 4MHz  ×85 = 340MHz (VCO)      */
#define CLOCK_PLLR_FIELD    0u      /* 340MHz ÷2 = 170MHz (SYSCLK)   */

/* 等 reg 的 mask 位变成 value；超时返回 -1。
   reg 必须是 volatile 寄存器，编译器不会把循环优化掉。 */
static int clock_wait(volatile uint32_t *reg, uint32_t mask, uint32_t value)
{
    uint32_t timeout = CLOCK_WAIT_TIMEOUT;
    while ((*reg & mask) != value) {
        if (--timeout == 0u) {
            return -1;
        }
    }
    return 0;
}

int bsp_clock_init(void)
{
    /* 第 1 步：打开 PWR 时钟（G4 的 PWR 挂在 APB1ENR1 上，不是 F4 的 APB1ENR） */
    RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN;
    (void)RCC->APB1ENR1;                    /* 读一次，等总线同步 */

    /* 第 2 步：切到 Boost 模式 + 电压 Range 1
       R1MODE=0 -> Boost（170MHz 必须）；VOS=01 -> Range 1。
       先写 CR5 再写 VOS，最后等 VOSF 清零。 */
    PWR->CR5 &= ~PWR_CR5_R1MODE;
    PWR->CR1 = (PWR->CR1 & ~PWR_CR1_VOS) | PWR_CR1_VOS_0;
    if (clock_wait(&PWR->SR2, PWR_SR2_VOSF, 0u) != 0) {
        SystemCoreClockUpdate();            /* 调压器未就绪，保持当前时钟 */
        return -1;
    }

    /* 第 3 步：如果当前不在 HSI 上，先切回 HSI。
       切时钟源只改 SW 字段，必须先清再写，避免旧值和新值按位或出保留值。 */
    RCC->CR |= RCC_CR_HSION;
    if (clock_wait(&RCC->CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY) != 0) {
        return -2;
    }
    if ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI) {
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_HSI;
        if (clock_wait(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_HSI) != 0) {
            SystemCoreClockUpdate();
            return -2;
        }
    }

    /* 第 4 步：关闭 PLL，等 PLLRDY 真正回落。
       RM0440：PLL 使能时 PLLCFGR 的配置位不可写，必须先关。 */
    RCC->CR &= ~RCC_CR_PLLON;
    if (clock_wait(&RCC->CR, RCC_CR_PLLRDY, 0u) != 0) {
        SystemCoreClockUpdate();
        return -3;
    }

    /* 第 5 步：Flash 等待周期
       170MHz / Range1 / Boost 需要 4WS；
       顺便开预取、指令缓存、数据缓存。
       在 16MHz HSI 下多等几拍没有副作用，所以先设好再升频。 */
    FLASH->ACR = FLASH_ACR_LATENCY_4WS
               | FLASH_ACR_PRFTEN
               | FLASH_ACR_ICEN
               | FLASH_ACR_DCEN;

    /* 第 6 步：总线分频（此刻跑在 HSI，改分频不会造成超频）
       HPRE=÷1  -> HCLK 170MHz
       PPRE1=÷1 -> APB1 170MHz
       PPRE2=÷1 -> APB2 170MHz
       这几个字段复位值不是 0，必须清空再赋值。 */
    RCC->CFGR &= ~(RCC_CFGR_HPRE | RCC_CFGR_PPRE1 | RCC_CFGR_PPRE2);
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1
               | RCC_CFGR_PPRE1_DIV1
               | RCC_CFGR_PPRE2_DIV1;

    /* 第 7 步：PLL 参数
       PLLSRC=HSE, PLLM=÷2, PLLN=×85, PLLR=÷2，并使能 R 输出（PLLREN）。
       PLLREN 必须置 1，否则 PLLR 无输出、SYSCLK 切不过去。 */
    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSE
                 | (CLOCK_PLLM_FIELD << RCC_PLLCFGR_PLLM_Pos)
                 | (CLOCK_PLLN_FIELD << RCC_PLLCFGR_PLLN_Pos)
                 | (CLOCK_PLLR_FIELD << RCC_PLLCFGR_PLLR_Pos)
                 | RCC_PLLCFGR_PLLREN;

    /* 第 8 步：打开 HSE 晶振，等它稳定（带超时） */
    RCC->CR |= RCC_CR_HSEON;
    if (clock_wait(&RCC->CR, RCC_CR_HSERDY, RCC_CR_HSERDY) != 0) {
        SystemCoreClockUpdate();            /* 留在 HSI，让延时按 16MHz 计算 */
        return -4;
    }

    /* 第 9 步：打开 PLL，等它锁定（带超时） */
    RCC->CR |= RCC_CR_PLLON;
    if (clock_wait(&RCC->CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY) != 0) {
        SystemCoreClockUpdate();
        return -5;
    }

    /* 第 10 步：把系统时钟源切换到 PLL，等切换完成（带超时）
       SWS 是"当前实际在用的时钟源"状态位，必须等它变成 PLL */
    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW) | RCC_CFGR_SW_PLL;
    if (clock_wait(&RCC->CFGR, RCC_CFGR_SWS, RCC_CFGR_SWS_PLL) != 0) {
        SystemCoreClockUpdate();
        return -6;
    }

    /* 第 11 步：更新 SystemCoreClock 全局变量
       system_stm32g4xx.c 维护的变量，bsp_delay_us 用它换算周期数 */
    SystemCoreClockUpdate();
    return 0;
}
