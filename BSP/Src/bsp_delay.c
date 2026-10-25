/**
 * @file  bsp_delay.c —— 延时实现
 *  毫秒: SysTick 定时器，1ms 中断一次（HAL_Delay 就是干这个的）
 *  微秒: DWT 硬件计数器，计数频率 = CPU 主频，不占任何外设
 */
#include "bsp.h"
#include "stm32g4xx.h"

static volatile uint32_t g_ms_tick = 0;    /* 毫秒计数值，中断里累加 */

/* 1 ms 钩子：BSP 里给空实现，App 可以定义同名强符号覆盖。
   中断里只做计数/标志类工作，重活留给主循环。 */
__attribute__((weak)) void bsp_tick_1ms_hook(void) { }

/* 启动文件 startup_stm32g431xx.s 里 SysTick_Handler 是弱符号，
   我们在这里定义强符号，链接时自动覆盖它 */
void SysTick_Handler(void)
{
    g_ms_tick++;
    bsp_tick_1ms_hook();
}

void bsp_delay_init(void)
{
    /* 第 1 步：SysTick 每 1ms 中断一次
       SystemCoreClock/1000 = 170MHz 下每个 1ms 计 170000 个数
       返回值非 0 说明重装值超出 24 位，当前主频下不会发生 */
    (void)SysTick_Config(SystemCoreClock / 1000UL);

    /* 第 2 步：启动 DWT 周期计数器
       注意：Cortex-M4 的 DWT【没有 LAR 锁寄存器】（LAR=0xC5ACCE55 解锁是
       Cortex-M7 及以后内核才有的步骤，写在 M4 上会直接访问到不存在的寄存器
       而编译报错）。M4 上电后 DWT 默认可用，使能 TRCENA 后直接开计数器即可 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* 开调试跟踪单元 */
    DWT->CYCCNT = 0;                                 /* 计数清零 */
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;             /* 开始计数 */
}

uint32_t bsp_tick_ms(void)
{
    return g_ms_tick;
}

void bsp_delay_ms(uint32_t ms)
{
    /* 记录起点，等差值 >= 目标值。
       用无符号减法做差值：即使 32 位计数翻转（49 天后）也不会出错。
       WFI 让 CPU 在等待期间睡眠，SysTick（或任意其它中断）到来时唤醒；
       醒来后重新判断，不会因为其它中断提前退出。 */
    uint32_t start = bsp_tick_ms();
    while ((bsp_tick_ms() - start) < ms) {
        __WFI();
    }
}

/* 单块微秒延时：调用者保证 us * cycles_per_us 不超过 32 位 */
static void delay_us_once(uint32_t us, uint32_t cycles_per_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * cycles_per_us;
    while ((DWT->CYCCNT - start) < ticks) { }
}

void bsp_delay_us(uint32_t us)
{
    /* DWT->CYCCNT 每个 CPU 周期 +1，170MHz 下 1us = 170 个计数。
       直接用 us * 170 计算会在 us 约 25.2s 时乘法溢出，
       而且 CYCCNT 本身也约 25.6 秒回绕一次。
       这里把超长延时拆成每块最多 30 亿周期的若干段，任意 us 都正确。 */
    uint32_t cycles_per_us = SystemCoreClock / 1000000UL;
    if (cycles_per_us == 0u) {
        return;                     /* 主频低于 1MHz 时无法提供微秒延时 */
    }

    uint32_t chunk_us = 3000000000UL / cycles_per_us;  /* < 2^32 周期，且 < 回绕周期 */
    while (us > chunk_us) {
        delay_us_once(chunk_us, cycles_per_us);
        us -= chunk_us;
    }
    delay_us_once(us, cycles_per_us);
}
