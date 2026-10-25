/**
 * @file  bsp_tim.c —— TIM6 10 kHz 快环节拍
 *
 * 170 MHz / 1700 = 100 kHz；ARR = 9 → 更新事件 10 kHz（100 µs）。
 * 快环中断优先级设为最高（0），保证 ADC 采样/保护判定不被通信打断。
 */
#include "bsp.h"
#include "stm32g4xx.h"

static volatile uint32_t s_fast_ticks;

void bsp_tim6_init(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_TIM6EN;
    (void)RCC->APB1ENR1;

    TIM6->CR1 = 0u;
    TIM6->PSC = 1700u - 1u;
    TIM6->ARR = 10u - 1u;
    TIM6->CR1 = TIM_CR1_ARPE;
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0u;
    TIM6->DIER = TIM_DIER_UIE;

    NVIC_SetPriority(TIM6_DAC_IRQn, 0u);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

void bsp_tim6_start(void)
{
    TIM6->CR1 |= TIM_CR1_CEN;
}

uint32_t bsp_fast_tick_count(void)
{
    return s_fast_ticks;
}

/* 启动文件里的弱符号 */
void TIM6_DAC_IRQHandler(void)
{
    if ((TIM6->SR & TIM_SR_UIF) != 0u) {
        TIM6->SR &= ~TIM_SR_UIF;
        s_fast_ticks++;
        bsp_fast_tick_hook();
    }
}
