/**
 * @file  bsp_vdac.c —— 外部 DAC80502 的板级绑定（SPI1 主机）
 *
 * SPI1：PB3=SCK / PB4=MISO / PB5=MOSI（AF5），PA8=CS（GPIO 推挽，低有效）。
 * 时钟 170 MHz / 16 = 10.6 MHz；mode 1。
 *
 * ★ 上电安全 ★
 *   DAC80502 复位默认输出 0 V，按本板注入公式 VOUT = 45 − 10×VDAC，
 *   0 V 对应 45 V 设定。因此 bsp_vdac_init() 在配置完成后立即写入
 *   "0 V 输出"对应的码（58982），整个过程 OUT_EN 保持禁止。
 */
#include "bsp.h"
#include "stm32g4xx.h"
#include "bsp_pins.h"
#include "dac80502.h"

#define SPI1_TIMEOUT  200000u

static void spi1_gpio_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    (void)RCC->AHB2ENR;

    /* PB3/4/5 → AF5 */
    GPIOB->MODER = (GPIOB->MODER & ~(3u << (3u * 2u))) | (2u << (3u * 2u));
    GPIOB->MODER = (GPIOB->MODER & ~(3u << (4u * 2u))) | (2u << (4u * 2u));
    GPIOB->MODER = (GPIOB->MODER & ~(3u << (5u * 2u))) | (2u << (5u * 2u));

    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(0xFu << (3u * 4u))) | (BSP_AF5_SPI << (3u * 4u));
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(0xFu << (4u * 4u))) | (BSP_AF5_SPI << (4u * 4u));
    GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(0xFu << (5u * 4u))) | (BSP_AF5_SPI << (5u * 4u));

    GPIOB->OTYPER &= ~(1u << 3u) & ~(1u << 4u) & ~(1u << 5u);
    GPIOB->OSPEEDR |= (3u << (3u * 2u)) | (3u << (4u * 2u)) | (3u << (5u * 2u));
}

static int spi1_xfer(const uint8_t *tx, uint32_t len)
{
    uint32_t timeout;

    PIN_VDAC_CS_GPIO->BSRR = (1u << (PIN_VDAC_CS_NUM + 16u));  /* CS 低 */

    for (uint32_t i = 0; i < len; i++) {
        timeout = SPI1_TIMEOUT;
        while ((SPI1->SR & SPI_SR_TXE) == 0u) {
            if (--timeout == 0u) {
                PIN_VDAC_CS_GPIO->BSRR = (1u << PIN_VDAC_CS_NUM);
                return -1;
            }
        }
        *(volatile uint8_t *)&SPI1->DR = tx[i];

        timeout = SPI1_TIMEOUT;
        while ((SPI1->SR & SPI_SR_RXNE) == 0u) {
            if (--timeout == 0u) {
                PIN_VDAC_CS_GPIO->BSRR = (1u << PIN_VDAC_CS_NUM);
                return -2;
            }
        }
        (void)*(volatile uint8_t *)&SPI1->DR;
    }

    PIN_VDAC_CS_GPIO->BSRR = (1u << PIN_VDAC_CS_NUM);          /* CS 高，锁存 */

    /* 等总线完全空闲再返回，避免下一帧过早开始 */
    timeout = SPI1_TIMEOUT;
    while ((SPI1->SR & SPI_SR_BSY) != 0u) {
        if (--timeout == 0u) {
            return -3;
        }
    }
    return 0;
}

void bsp_vdac_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;

    spi1_gpio_init();

    SPI1->CR1 = 0u;
    SPI1->CR2 = (7u << SPI_CR2_DS_Pos);            /* 8-bit */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                SPI_CR1_CPHA |                     /* mode 1 */
                (3u << SPI_CR1_BR_Pos) |           /* 170/16 = 10.6 MHz */
                SPI_CR1_SPE;

    dac80502_bind(spi1_xfer);
    (void)dac80502_init();

    /* ★ 必须在允许输出之前写入 0 V 设定 ★ */
    (void)bsp_vdac_set_vset_code(58982u);          /* VDAC = 4.500 V → VOUT = 0 V */
    (void)bsp_vdac_set_iset_code(0u);
}

int bsp_vdac_set_vset_code(uint16_t code)
{
    return dac80502_set_channel(0u, code);
}

int bsp_vdac_set_iset_code(uint16_t code)
{
    return dac80502_set_channel(1u, code);
}

int bsp_vdac_write_all(uint16_t vset_code, uint16_t iset_code)
{
    return dac80502_set_channels(vset_code, iset_code);
}
