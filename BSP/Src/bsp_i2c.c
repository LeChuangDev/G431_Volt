/**
 * @file  bsp_i2c.c —— I2C1 主/从复用
 *
 * 总线拓扑（PB6/PB7，AF4，开漏，外部 4.7 kΩ 上拉）：
 *   G431  ── 主机 ──► CH224Q（0x23，PD 协商）
 *   G431  ◄─ 从机 ─── ESP32（0x24，命令下行）
 * 两个方向分时使用：CH224Q 每 100 ms 轮询一次（<1 ms），其余时间 G431 监听
 * 从机地址。若 ESP32 恰好撞上轮询窗口会被 NACK，重发即可。
 *
 * 从机命令在 STOP 时整帧入队，主循环调用 bsp_i2c_take_command() 取走，
 * 中断里不做任何业务处理。
 */
#include "bsp.h"
#include "stm32g4xx.h"
#include "bsp_pins.h"

#define I2C_TIMEOUT_LOOPS  100000u
#define I2C_SLAVE_ADDR     BSP_I2C_CMD_ADDR

/* 400 kHz @ 170 MHz：PRESC=0, SCLDEL=16, SDADEL=3, SCLH=140, SCLL=283 */
#define I2C_TIMING_400K    0x01038D1Bu

typedef struct {
    uint8_t len;
    uint8_t data[BSP_I2C_CMD_MAX];
} i2c_cmd_t;

static i2c_cmd_t        s_cmd[BSP_I2C_CMD_SLOTS];
static volatile uint8_t s_head;
static volatile uint8_t s_tail;

static volatile uint8_t s_rx_buf[BSP_I2C_CMD_MAX];
static volatile uint8_t s_rx_len;
static volatile bool    s_rx_active;

/* ------------------------------------------------------------------ */
/* 公共                                                               */
/* ------------------------------------------------------------------ */
static int i2c_wait_set(volatile uint32_t *reg, uint32_t mask, uint32_t timeout)
{
    while ((*reg & mask) == 0u) {
        if (--timeout == 0u) {
            return -1;
        }
    }
    return 0;
}

void bsp_i2c1_init(void)
{
    RCC->APB1ENR1 |= RCC_APB1ENR1_I2C1EN;
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    (void)RCC->APB1ENR1;
    (void)RCC->AHB2ENR;

    /* PB6/PB7 → AF4，开漏，外部上拉 */
    const uint32_t pins[2] = { 6u, 7u };
    for (uint32_t i = 0; i < 2u; i++) {
        uint32_t p = pins[i];
        GPIOB->MODER = (GPIOB->MODER & ~(3u << (p * 2u))) | (2u << (p * 2u));
        GPIOB->OTYPER |= (1u << p);
        GPIOB->OSPEEDR |= (3u << (p * 2u));
        GPIOB->PUPDR &= ~(3u << (p * 2u));
        uint32_t sh = (p & 7u) * 4u;
        GPIOB->AFR[0] = (GPIOB->AFR[0] & ~(0xFu << sh)) | (BSP_AF4_I2C << sh);
    }

    I2C1->CR1 = 0u;
    I2C1->TIMINGR = I2C_TIMING_400K;
    I2C1->OAR1 = ((uint32_t)I2C_SLAVE_ADDR << 1) | I2C_OAR1_OA1EN;

    I2C1->CR1 = I2C_CR1_PE | I2C_CR1_ADDRIE | I2C_CR1_RXIE |
                I2C_CR1_TXIE | I2C_CR1_STOPIE | I2C_CR1_NACKIE;

    NVIC_SetPriority(I2C1_EV_IRQn, 3u);
    NVIC_SetPriority(I2C1_ER_IRQn, 3u);
    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ER_IRQn);

    s_head = s_tail = 0u;
    s_rx_len = 0u;
    s_rx_active = false;
}

/* ------------------------------------------------------------------ */
/* 主机                                                               */
/* ------------------------------------------------------------------ */
static void i2c_clear_flags(void)
{
    I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF |
                I2C_ICR_ARLOCF | I2C_ICR_BERRCF | I2C_ICR_OVRCF;
}

static int i2c_wait_bus_free(void)
{
    uint32_t timeout = I2C_TIMEOUT_LOOPS;
    while ((I2C1->ISR & I2C_ISR_BUSY) != 0u) {
        if (--timeout == 0u) {
            return -1;
        }
    }
    return 0;
}

int bsp_i2c_master_write(uint8_t addr7, const uint8_t *data, uint32_t len)
{
    if (len == 0u || len > 255u) {
        return -1;
    }
    if (i2c_wait_bus_free() != 0) {
        return -2;
    }
    i2c_clear_flags();

    I2C1->CR2 = ((uint32_t)addr7 << 1) |
                ((uint32_t)len << 16) |
                I2C_CR2_START | I2C_CR2_AUTOEND;

    for (uint32_t i = 0; i < len; i++) {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        for (;;) {
            uint32_t isr = I2C1->ISR;
            if ((isr & I2C_ISR_NACKF) != 0u) {
                I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
                return -3;
            }
            if ((isr & I2C_ISR_ARLO) != 0u) {
                I2C1->ICR = I2C_ICR_ARLOCF;
                return -4;
            }
            if ((isr & I2C_ISR_TXIS) != 0u) {
                break;
            }
            if (--timeout == 0u) {
                return -5;
            }
        }
        I2C1->TXDR = data[i];
    }

    if (i2c_wait_set(&I2C1->ISR, I2C_ISR_STOPF, I2C_TIMEOUT_LOOPS) != 0) {
        return -6;
    }
    i2c_clear_flags();
    return 0;
}

int bsp_i2c_master_read(uint8_t addr7, uint8_t *data, uint32_t len)
{
    if (len == 0u || len > 255u) {
        return -1;
    }
    if (i2c_wait_bus_free() != 0) {
        return -2;
    }
    i2c_clear_flags();

    I2C1->CR2 = ((uint32_t)addr7 << 1) |
                I2C_CR2_RD_WRN |
                ((uint32_t)len << 16) |
                I2C_CR2_START | I2C_CR2_AUTOEND;

    for (uint32_t i = 0; i < len; i++) {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        for (;;) {
            uint32_t isr = I2C1->ISR;
            if ((isr & I2C_ISR_NACKF) != 0u) {
                I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
                return -3;
            }
            if ((isr & I2C_ISR_ARLO) != 0u) {
                I2C1->ICR = I2C_ICR_ARLOCF;
                return -4;
            }
            if ((isr & I2C_ISR_RXNE) != 0u) {
                break;
            }
            if (--timeout == 0u) {
                return -5;
            }
        }
        data[i] = (uint8_t)I2C1->RXDR;
    }

    if (i2c_wait_set(&I2C1->ISR, I2C_ISR_STOPF, I2C_TIMEOUT_LOOPS) != 0) {
        return -6;
    }
    i2c_clear_flags();
    return 0;
}

int bsp_i2c_master_write_read(uint8_t addr7, const uint8_t *wr, uint32_t wn,
                              uint8_t *rd, uint32_t rn)
{
    if (wn == 0u || wn > 255u || rn == 0u || rn > 255u) {
        return -1;
    }
    if (i2c_wait_bus_free() != 0) {
        return -2;
    }
    i2c_clear_flags();

    /* 阶段 1：写寄存器地址，不发 STOP */
    I2C1->CR2 = ((uint32_t)addr7 << 1) |
                ((uint32_t)wn << 16) |
                I2C_CR2_START;

    for (uint32_t i = 0; i < wn; i++) {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        for (;;) {
            uint32_t isr = I2C1->ISR;
            if ((isr & I2C_ISR_NACKF) != 0u) {
                I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
                return -3;
            }
            if ((isr & I2C_ISR_TXIS) != 0u) {
                break;
            }
            if (--timeout == 0u) {
                return -5;
            }
        }
        I2C1->TXDR = wr[i];
    }

    if (i2c_wait_set(&I2C1->ISR, I2C_ISR_TC, I2C_TIMEOUT_LOOPS) != 0) {
        return -6;
    }

    /* 阶段 2：重复起始 + 读 */
    I2C1->CR2 = ((uint32_t)addr7 << 1) |
                I2C_CR2_RD_WRN |
                ((uint32_t)rn << 16) |
                I2C_CR2_START | I2C_CR2_AUTOEND;

    for (uint32_t i = 0; i < rn; i++) {
        uint32_t timeout = I2C_TIMEOUT_LOOPS;
        for (;;) {
            uint32_t isr = I2C1->ISR;
            if ((isr & I2C_ISR_NACKF) != 0u) {
                I2C1->ICR = I2C_ICR_NACKCF | I2C_ICR_STOPCF;
                return -3;
            }
            if ((isr & I2C_ISR_RXNE) != 0u) {
                break;
            }
            if (--timeout == 0u) {
                return -5;
            }
        }
        rd[i] = (uint8_t)I2C1->RXDR;
    }

    if (i2c_wait_set(&I2C1->ISR, I2C_ISR_STOPF, I2C_TIMEOUT_LOOPS) != 0) {
        return -6;
    }
    i2c_clear_flags();
    return 0;
}

/* ------------------------------------------------------------------ */
/* 从机命令接收                                                       */
/* ------------------------------------------------------------------ */
bool bsp_i2c_take_command(uint8_t *buf, uint32_t buf_len, uint32_t *out_len)
{
    if (s_tail == s_head) {
        return false;
    }
    i2c_cmd_t *c = &s_cmd[s_tail];
    uint32_t n = c->len;
    if (n > buf_len) {
        n = buf_len;
    }
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = c->data[i];
    }
    *out_len = n;
    s_tail = (uint8_t)((s_tail + 1u) % BSP_I2C_CMD_SLOTS);
    return true;
}

static void i2c_slave_finish_frame(void)
{
    if (!s_rx_active) {
        return;
    }
    s_rx_active = false;

    if (s_rx_len < 4u) {
        return;
    }
    /* 简单帧头校验，减少总线噪声造成的误动作 */
    if (s_rx_buf[0] != 0x5Au) {
        return;
    }

    uint8_t next = (uint8_t)((s_head + 1u) % BSP_I2C_CMD_SLOTS);
    if (next == s_tail) {
        return;                             /* 队列满，丢弃 */
    }
    s_cmd[s_head].len = (uint8_t)s_rx_len;
    for (uint32_t i = 0; i < s_rx_len; i++) {
        s_cmd[s_head].data[i] = s_rx_buf[i];
    }
    s_head = next;
}

void I2C1_EV_IRQHandler(void)
{
    uint32_t isr = I2C1->ISR;

    if ((isr & I2C_ISR_ADDR) != 0u) {
        if ((isr & I2C_ISR_DIR) != 0u) {
            /* 主设备要读我们：本设计不提供读数据，先送 0xFF 占位 */
            I2C1->TXDR = 0xFFu;
        } else {
            s_rx_active = true;
            s_rx_len = 0u;
        }
        I2C1->ICR = I2C_ICR_ADDRCF;
    }

    if ((isr & I2C_ISR_RXNE) != 0u) {
        uint8_t b = (uint8_t)I2C1->RXDR;
        if (s_rx_active && s_rx_len < BSP_I2C_CMD_MAX) {
            s_rx_buf[s_rx_len++] = b;
        }
    }

    if ((isr & I2C_ISR_TXIS) != 0u) {
        I2C1->TXDR = 0xFFu;
    }

    if ((isr & I2C_ISR_NACKF) != 0u) {
        I2C1->ICR = I2C_ICR_NACKCF;
    }

    if ((isr & I2C_ISR_STOPF) != 0u) {
        I2C1->ICR = I2C_ICR_STOPCF;
        i2c_slave_finish_frame();
    }
}

void I2C1_ER_IRQHandler(void)
{
    uint32_t isr = I2C1->ISR;
    if ((isr & I2C_ISR_BERR) != 0u) {
        I2C1->ICR = I2C_ICR_BERRCF;
    }
    if ((isr & I2C_ISR_ARLO) != 0u) {
        I2C1->ICR = I2C_ICR_ARLOCF;
    }
    if ((isr & I2C_ISR_OVR) != 0u) {
        I2C1->ICR = I2C_ICR_OVRCF;
        s_rx_active = false;
    }
    if ((isr & I2C_ISR_NACKF) != 0u) {
        I2C1->ICR = I2C_ICR_NACKCF;
    }
}
