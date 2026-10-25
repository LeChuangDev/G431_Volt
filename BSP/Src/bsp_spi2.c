/**
 * @file  bsp_spi2.c —— SPI2 从机遥测链路（G431 → ESP32）
 *
 * 为什么用 DMA：
 *   ESP32 以最高 10 MHz 时钟读帧，字节间隔只有 0.8 µs。若用 RXNE/TXE 中断，
 *   很容易被 10 kHz 快环打断而丢字节。这里 TX 用 DMA 自动喂数，RX 用
 *   循环 DMA 丢进垃圾桶（本链路命令走 I2C），CPU 只在整帧结束时收一次中断。
 *
 * 帧管理：双缓冲。
 *   主循环写"非活动"缓冲并置 pending；DMA 传输完成中断里切换到新缓冲并
 *   重新装载计数。这样主循环更新遥测时不会改到正在发送的那一帧。
 *
 * 链路监视：
 *   连续 100 ms 没有被时钟 → link_ok = false（只标记，不影响输出）。
 *   连续 500 ms 没有完成帧 → 强制复位并重装 DMA（从机可能被短帧卡住）。
 */
#include "bsp.h"
#include "stm32g4xx.h"
#include "bsp_pins.h"

/* DMAMUX1 请求号（RM0440 表 "DMAMUX1 request mapping"） */
#define DMAMUX_REQ_SPI2_RX   7u
#define DMAMUX_REQ_SPI2_TX   8u

static uint8_t  s_tx[2][BSP_SPI_FRAME_LEN];
static uint8_t  s_rx_dump[BSP_SPI_FRAME_LEN];
static volatile uint8_t s_active;
static volatile bool    s_pending;
static volatile uint32_t s_frame_count;
static volatile uint32_t s_last_frame_ms;

static void dma_tx_arm(uint8_t idx)
{
    DMA1_Channel5->CCR &= ~DMA_CCR_EN;
    DMA1_Channel5->CPAR = (uint32_t)&SPI2->DR;
    DMA1_Channel5->CMAR = (uint32_t)s_tx[idx];
    DMA1_Channel5->CNDTR = BSP_SPI_FRAME_LEN;
    DMA1_Channel5->CCR |= DMA_CCR_EN;
}

static void dma_rx_arm(void)
{
    DMA1_Channel4->CCR &= ~DMA_CCR_EN;
    DMA1_Channel4->CPAR = (uint32_t)&SPI2->DR;
    DMA1_Channel4->CMAR = (uint32_t)s_rx_dump;
    DMA1_Channel4->CNDTR = BSP_SPI_FRAME_LEN;
    DMA1_Channel4->CCR |= DMA_CCR_EN;
}

void bsp_spi2_slave_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMAMUX1EN;
    RCC->APB1ENR1 |= RCC_APB1ENR1_SPI2EN;
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    (void)RCC->AHB1ENR;
    (void)RCC->APB1ENR1;
    (void)RCC->AHB2ENR;

    /* PB13 SCK / PB14 MISO / PB15 MOSI / PB9 NSS → AF5 */
    const uint32_t pins[4] = { 13u, 14u, 15u, 9u };
    for (uint32_t i = 0; i < 4u; i++) {
        uint32_t p = pins[i];
        GPIOB->MODER = (GPIOB->MODER & ~(3u << (p * 2u))) | (2u << (p * 2u));
        GPIOB->OTYPER &= ~(1u << p);
        GPIOB->OSPEEDR |= (3u << (p * 2u));
        GPIOB->PUPDR &= ~(3u << (p * 2u));
        uint32_t sh = (p & 7u) * 4u;
        GPIOB->AFR[p >> 3] = (GPIOB->AFR[p >> 3] & ~(0xFu << sh)) |
                             (BSP_AF5_SPI << sh);
    }

    /* ---- DMA 通道 ---- */
    DMAMUX1_Channel4->CCR = DMAMUX_REQ_SPI2_RX;
    DMAMUX1_Channel5->CCR = DMAMUX_REQ_SPI2_TX;

    /* RX：外设→内存，8-bit，循环（内容丢弃） */
    DMA1_Channel4->CCR = DMA_CCR_MINC | DMA_CCR_CIRC;
    DMA1_Channel4->CCR |= (2u << DMA_CCR_PL_Pos);
    dma_rx_arm();

    /* TX：内存→外设，8-bit，整帧完成中断 */
    DMA1_Channel5->CCR = DMA_CCR_DIR | DMA_CCR_MINC | DMA_CCR_TCIE;
    DMA1_Channel5->CCR |= (2u << DMA_CCR_PL_Pos);
    NVIC_SetPriority(DMA1_Channel5_IRQn, 2u);
    NVIC_EnableIRQ(DMA1_Channel5_IRQn);
    dma_tx_arm(0u);

    /* ---- SPI2 从机：mode 0，硬件 NSS ---- */
    SPI2->CR1 = 0u;
    SPI2->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN;
    SPI2->CR1 = SPI_CR1_SPE;          /* MSTR=0, SSM=0, CPOL=0, CPHA=0 */

    s_active = 0u;
    s_pending = false;
    s_frame_count = 0u;
    s_last_frame_ms = bsp_tick_ms();
}

void bsp_spi2_slave_set_frame(const uint8_t *data, uint32_t len)
{
    if (len > BSP_SPI_FRAME_LEN) {
        len = BSP_SPI_FRAME_LEN;
    }
    /* DMA 完成中断优先级高于本函数所在上下文（SysTick），
       若在拷贝中途切换缓冲会发出一帧撕裂数据：拷贝期间关中断。
       32 字节拷贝 ≈ 1 µs，代价可以忽略。 */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    uint8_t idx = (uint8_t)(s_active ^ 1u);
    for (uint32_t i = 0; i < len; i++) {
        s_tx[idx][i] = data[i];
    }
    for (uint32_t i = len; i < BSP_SPI_FRAME_LEN; i++) {
        s_tx[idx][i] = 0u;
    }
    s_pending = true;

    __set_PRIMASK(primask);
}

uint32_t bsp_spi2_slave_frame_count(void)
{
    return s_frame_count;
}

bool bsp_spi2_slave_link_ok(void)
{
    return (bsp_tick_ms() - s_last_frame_ms) < 100u;
}

void bsp_spi2_slave_poll(void)
{
    static uint32_t s_last_recover_ms;

    uint32_t now = bsp_tick_ms();
    if ((now - s_last_frame_ms) > 500u && (now - s_last_recover_ms) > 500u) {
        /* 从机被短帧/异常 NSS 卡住：强制复位收发通道。
           注意不要动 s_last_frame_ms —— link_ok 必须反映"真的收到过帧" */
        s_last_recover_ms = now;
        SPI2->CR1 &= ~SPI_CR1_SPE;
        dma_tx_arm(s_active);
        dma_rx_arm();
        SPI2->CR1 |= SPI_CR1_SPE;
    }
}

void DMA1_Channel5_IRQHandler(void)
{
    if ((DMA1->ISR & DMA_ISR_TCIF5) != 0u) {
        DMA1->IFCR = DMA_IFCR_CTCIF5;

        s_frame_count++;
        s_last_frame_ms = bsp_tick_ms();

        if (s_pending) {
            s_active ^= 1u;
            s_pending = false;
        }
        dma_tx_arm(s_active);

        /* 清掉 SPI 溢出/残留（RX 由循环 DMA 取走，这里只是兜底） */
        if ((SPI2->SR & SPI_SR_OVR) != 0u) {
            (void)SPI2->DR;
            (void)SPI2->SR;
        }
    }
}
