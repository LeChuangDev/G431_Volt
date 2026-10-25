/**
 * @file  bsp_dac.c —— 内部 DAC1 通道 1（PA4 / ICTRL 输入限流控制电压）
 *
 * 0..3300 mV 对应输入限流 0..4.5 A（比例见 bsp_board.h）。
 * 只在 1 kHz 中环更新，输出缓冲器开启，无触发（写 DHR 立即生效）。
 */
#include "bsp.h"
#include "stm32g4xx.h"
#include "bsp_pins.h"

void bsp_dac_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_DAC1EN;
    (void)RCC->AHB2ENR;

    DAC1->CR = 0u;                                  /* 先关，改配置 */
    DAC1->MCR = (DAC1->MCR & ~DAC_MCR_MODE1) | (0u << DAC_MCR_MODE1_Pos);

    bsp_delay_us(10);                               /* 建立时间 */
    DAC1->DHR12R1 = 0u;
    DAC1->CR |= DAC_CR_EN1;
    bsp_delay_us(10);
}

void bsp_dac_set_mv(uint32_t mv)
{
    if (mv > BSP_ICTRL_FULL_MV) {
        mv = BSP_ICTRL_FULL_MV;
    }
    uint32_t code = (mv * 4095u + BSP_ICTRL_FULL_MV / 2u) / BSP_ICTRL_FULL_MV;
    DAC1->DHR12R1 = code & 0x0FFFu;
}
