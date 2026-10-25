/**
 * @file  bsp_gpio.c —— 控制引脚、故障输入、PROT_TRIP 外部中断、心跳
 *
 * ★ 安全默认 ★
 *   OUT_EN 低有效：复位后必须立即把 PB10 驱到高电平（禁止输出）。
 *   本函数在 main() 里紧跟时钟初始化调用，早于 ADC/DAC/通信初始化。
 *   硬件上仍建议给 PB10 加 10 kΩ 上拉，覆盖"固件尚未运行"的窗口。
 */
#include "bsp.h"
#include "stm32g4xx.h"
#include "bsp_pins.h"

static volatile uint32_t s_trip_count;
static volatile bool     s_trip_flag;
static bool              s_out_enabled;

static void gpio_clock_enable(GPIO_TypeDef *port)
{
    if (port == GPIOA) {
        RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    } else if (port == GPIOB) {
        RCC->AHB2ENR |= RCC_AHB2ENR_GPIOBEN;
    } else {
        RCC->AHB2ENR |= RCC_AHB2ENR_GPIOCEN;
    }
    (void)RCC->AHB2ENR;
}

static void gpio_output(GPIO_TypeDef *port, uint32_t pin, bool level)
{
    gpio_clock_enable(port);

    /* 先写 ODR 再切输出模式，避免切换瞬间输出错误电平 */
    if (level) {
        port->BSRR = (1u << pin);
    } else {
        port->BSRR = (1u << (pin + 16u));
    }
    port->OTYPER &= ~(1u << pin);                       /* 推挽 */
    port->OSPEEDR |= (2u << (pin * 2u));                /* 中速即可 */
    port->PUPDR &= ~(3u << (pin * 2u));
    port->MODER = (port->MODER & ~(3u << (pin * 2u))) | (1u << (pin * 2u));
}

static void gpio_input(GPIO_TypeDef *port, uint32_t pin, uint32_t pupd)
{
    gpio_clock_enable(port);
    port->MODER &= ~(3u << (pin * 2u));                 /* 输入 */
    port->PUPDR = (port->PUPDR & ~(3u << (pin * 2u))) | (pupd << (pin * 2u));
}

static void gpio_analog(GPIO_TypeDef *port, uint32_t pin)
{
    gpio_clock_enable(port);
    port->MODER |= (3u << (pin * 2u));                  /* 模拟 */
    port->PUPDR &= ~(3u << (pin * 2u));
}

void bsp_gpio_init(void)
{
    /* ---- 先把"禁止输出"落实，再配置其它引脚 ---- */
    gpio_output(PIN_OUT_EN_GPIO, PIN_OUT_EN_NUM, true);   /* 高 = 禁止 */
    s_out_enabled = false;

    gpio_output(PIN_SINK_EN_GPIO, PIN_SINK_EN_NUM, false);
    gpio_output(PIN_HB_OUT_GPIO, PIN_HB_OUT_NUM, false);
    gpio_output(PIN_DATA_RDY_GPIO, PIN_DATA_RDY_NUM, false);
    gpio_output(PIN_VDAC_CS_GPIO, PIN_VDAC_CS_NUM, true); /* CS 空闲高 */

    /* nFAULT / PROT_TRIP 低有效，内部上拉；HB_IN 由 ESP32 推挽驱动 */
    gpio_input(PIN_NFAULT_GPIO, PIN_NFAULT_NUM, 1u);
    gpio_input(PIN_PROT_TRIP_GPIO, PIN_PROT_TRIP_NUM, 1u);
    gpio_input(PIN_HB_IN_GPIO, PIN_HB_IN_NUM, 2u);       /* 下拉，断开时读到 0 */

    /* 模拟脚全部设为模拟模式，避免浮空输入耗电/串扰 */
    gpio_analog(PIN_VOUT_GPIO, PIN_VOUT_NUM);
    gpio_analog(PIN_IOUT_GPIO, PIN_IOUT_NUM);
    gpio_analog(PIN_VIN_GPIO, PIN_VIN_NUM);
    gpio_analog(PIN_IIN_GPIO, PIN_IIN_NUM);
    gpio_analog(PIN_ICTRL_GPIO, PIN_ICTRL_NUM);
    gpio_analog(PIN_NTC_GPIO, PIN_NTC_NUM);
    gpio_analog(PIN_OCP_GPIO, PIN_OCP_NUM);
    gpio_analog(PIN_OVP_GPIO, PIN_OVP_NUM);

    /* ---- PA15 下降沿 EXTI ---- */
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;
    (void)RCC->APB2ENR;

    SYSCFG->EXTICR[3] = (SYSCFG->EXTICR[3] & ~SYSCFG_EXTICR4_EXTI15) |
                        SYSCFG_EXTICR4_EXTI15_PA;
    EXTI->FTSR1 |= (1u << 15);
    EXTI->RTSR1 &= ~(1u << 15);
    EXTI->PR1 = (1u << 15);                  /* 清掉上电残留 */
    EXTI->IMR1 |= (1u << 15);

    /* ★ EXTI 线使能后还必须打开 NVIC，否则中断永远不会进 */
    NVIC_SetPriority(EXTI15_10_IRQn, 0u);    /* 与快环同级：立即响应 */
    NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void bsp_out_enable(bool enable)
{
    /* OUT_EN 低有效：enable=true → 拉低 */
    if (enable) {
        PIN_OUT_EN_GPIO->BSRR = (1u << (PIN_OUT_EN_NUM + 16u));
    } else {
        PIN_OUT_EN_GPIO->BSRR = (1u << PIN_OUT_EN_NUM);
    }
    s_out_enabled = enable;
}

bool bsp_out_is_enabled(void)
{
    return s_out_enabled;
}

void bsp_sink_enable(bool enable)
{
    if (enable) {
        PIN_SINK_EN_GPIO->BSRR = (1u << PIN_SINK_EN_NUM);
    } else {
        PIN_SINK_EN_GPIO->BSRR = (1u << (PIN_SINK_EN_NUM + 16u));
    }
}

bool bsp_nfault_asserted(void)
{
    return (PIN_NFAULT_GPIO->IDR & (1u << PIN_NFAULT_NUM)) == 0u;
}

bool bsp_hb_in_level(void)
{
    return (PIN_HB_IN_GPIO->IDR & (1u << PIN_HB_IN_NUM)) != 0u;
}

void bsp_hb_out_set(bool level)
{
    if (level) {
        PIN_HB_OUT_GPIO->BSRR = (1u << PIN_HB_OUT_NUM);
    } else {
        PIN_HB_OUT_GPIO->BSRR = (1u << (PIN_HB_OUT_NUM + 16u));
    }
}

void bsp_data_ready_set(bool level)
{
    if (level) {
        PIN_DATA_RDY_GPIO->BSRR = (1u << PIN_DATA_RDY_NUM);
    } else {
        PIN_DATA_RDY_GPIO->BSRR = (1u << (PIN_DATA_RDY_NUM + 16u));
    }
}

bool bsp_prot_trip_active(void)
{
    return s_trip_flag;
}

uint32_t bsp_prot_trip_count(void)
{
    return s_trip_count;
}

void bsp_prot_trip_clear(void)
{
    s_trip_flag = false;
}

/* 启动文件里的弱符号，这里定义强符号 */
void EXTI15_10_IRQHandler(void)
{
    if ((EXTI->PR1 & (1u << 15)) != 0u) {
        EXTI->PR1 = (1u << 15);          /* 写 1 清除 */
        s_trip_count++;
        s_trip_flag = true;
    }
}
