/**
 * @file  bsp_uart.c —— USART1 调试日志（PA9 TX / PA10 RX，115200 8N1）
 *
 * 阻塞发送。只在主循环里调用（不要在 10 kHz / 1 kHz 中断里打印）。
 */
#include "bsp.h"
#include "bsp_pins.h"
#include "stm32g4xx.h"

void bsp_uart_init(uint32_t baud)
{
    if (baud == 0u) {
        baud = 115200u;
    }

    RCC->APB2ENR |= RCC_APB2ENR_USART1EN | RCC_APB2ENR_SYSCFGEN;
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    (void)RCC->APB2ENR;
    (void)RCC->AHB2ENR;

    /* PA9/PA10 → AF7 */
    const uint32_t pins[2] = { 9u, 10u };
    for (uint32_t i = 0; i < 2u; i++) {
        uint32_t p = pins[i];
        GPIOA->MODER = (GPIOA->MODER & ~(3u << (p * 2u))) | (2u << (p * 2u));
        GPIOA->OTYPER &= ~(1u << p);
        GPIOA->OSPEEDR |= (3u << (p * 2u));
        GPIOA->PUPDR = (GPIOA->PUPDR & ~(3u << (p * 2u))) |
                       (1u << (p * 2u));         /* TX 上拉，空闲高 */
        uint32_t sh = (p & 7u) * 4u;
        GPIOA->AFR[p >> 3] = (GPIOA->AFR[p >> 3] & ~(0xFu << sh)) |
                             (BSP_AF7_USART << sh);
    }

    USART1->CR1 = 0u;
    /* 16 倍过采样：BRR = fCK / baud（四舍五入） */
    USART1->BRR = (uint32_t)((SystemCoreClock + baud / 2u) / baud) & 0xFFFFu;
    USART1->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;
}

void bsp_uart_putc(char c)
{
    while ((USART1->ISR & USART_ISR_TXE) == 0u) { }
    USART1->TDR = (uint8_t)c;
}

void bsp_uart_write(const char *s)
{
    if (s == 0) {
        return;
    }
    while (*s != '\0') {
        bsp_uart_putc(*s++);
    }
    while ((USART1->ISR & USART_ISR_TC) == 0u) { }
}

void bsp_uart_write_u32(uint32_t v)
{
    char buf[11];
    int i = 10;
    buf[i] = '\0';
    if (v == 0u) {
        buf[--i] = '0';
    } else {
        while (v > 0u && i > 0) {
            buf[--i] = (char)('0' + (v % 10u));
            v /= 10u;
        }
    }
    bsp_uart_write(&buf[i]);
}
