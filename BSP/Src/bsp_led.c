/**
 * @file  bsp_led.c —— 板载 LED（模板默认 PB2 推挽输出）
 *
 * 换板子时只改下面四个宏，App 层不需要动：
 *   LED_GPIO / LED_PIN / LED_RCC_EN / LED_ACTIVE_LEVEL
 *
 * ★ 为什么不用位带(bit-band) ★
 *   STM32G4 的 GPIO 寄存器位于 0x4800_0000，而 Cortex-M4 外设位带区只覆盖
 *   0x4000_0000~0x400F_FFFF —— GPIO 不在其中，位带别名地址写下去是无效的。
 *   因此这里改用 GPIOx->BSRR（置位/复位寄存器）：写 1 生效、写 0 无影响，
 *   天然就是原子操作，不会破坏同端口其它引脚。
 *
 * 注意：PB2 在部分 G431 板子上用于 VBUS 检测/模拟输入。例如 WeAct
 * STM32G431 Core Board 的板载 LED 实际接在 PC6（高电平点亮），那就把
 * 上面四个宏改成 GPIOC / 6 / RCC_AHB2ENR_GPIOCEN / 1。改板只动这里。
 */
#include "bsp.h"
#include "stm32g4xx.h"

#define LED_GPIO          GPIOB
#define LED_PIN           2u
#define LED_RCC_EN        RCC_AHB2ENR_GPIOBEN   /* G4 的 GPIO 挂在 AHB2 上 */
#define LED_ACTIVE_LEVEL  1u        /* 1 = 高电平点亮；低电平点亮的板改成 0 */

static inline void led_write(uint32_t level)
{
    if ((level != 0u) == (LED_ACTIVE_LEVEL != 0u)) {
        LED_GPIO->BSRR = (1u << LED_PIN);          /* 置 1 */
    } else {
        LED_GPIO->BSRR = (1u << (LED_PIN + 16u));  /* 清 0 */
    }
}

void bsp_led_init(void)
{
    /* 开 GPIO 时钟；读回一次寄存器，等外设总线时钟稳定后再访问 GPIO */
    RCC->AHB2ENR |= LED_RCC_EN;
    (void)RCC->AHB2ENR;

    /* MODER：先清 2 位，再写 01 = 通用推挽输出。
       OTYPER/OSPEEDR/PUPDR 保持复位值即可（推挽、低速、无上下拉） */
    LED_GPIO->MODER &= ~(3u << (LED_PIN * 2u));
    LED_GPIO->MODER |=  (1u << (LED_PIN * 2u));

    bsp_led_off();
}

void bsp_led_on(void)
{
    led_write(1u);
}

void bsp_led_off(void)
{
    led_write(0u);
}

void bsp_led_toggle(void)
{
    /* 读当前输出位 -> 用 BSRR 原子地写反，不碰同端口其它位 */
    led_write((LED_GPIO->ODR & (1u << LED_PIN)) != 0u ? 0u : 1u);
}
