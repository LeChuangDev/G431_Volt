/**
 * @file  main.c —— LeVolt 数控电源主程序
 *
 * 初始化顺序是安全相关的，不要随意调整：
 *   1. 时钟 / 延时
 *   2. GPIO：★ 第一件事就是把 OUT_EN 置高（输出禁止）
 *   3. 外设：ADC、内部 DAC(ICTRL)、外部 DAC80502（先写 0 V 对应码）、
 *      SPI2 从机、I2C1、CH224Q 绑定、TIM6
 *   4. App 初始化（各模块状态清零）
 *   5. 打开 IWDG，最后启动 10 kHz 快环
 *
 * 外部 DAC80502 复位后输出码 = 0 → VDAC = 0 V → VOUT = 45 V，
 * 因此【任何情况下】都必须在 OUT_EN 有效之前把 VSET 写成 0 V 码，
 * bsp_vdac_init() 和 app_control_init() 都会做这件事。
 */
#include "bsp.h"
#include "app_power.h"
#include "app_config.h"

int main(void)
{
    (void)bsp_clock_init();     /* 170 MHz；失败时回退 HSI 继续跑 */
    bsp_delay_init();           /* SysTick 1 ms（App 钩子此时还没使能） */

    /* ★ 先建立安全电平：PB10 高 = 源出关闭，PB11 低 = 下拉关闭 */
    bsp_gpio_init();
    bsp_led_init();
    bsp_uart_init(115200u);

    bsp_adc_init();
    bsp_dac_init();
    bsp_vdac_init();            /* 立即写入 0 V 对应码 58982 */

    bsp_spi2_slave_init();
    bsp_i2c1_init();
    bsp_pd_init();
    bsp_tim6_init();

    app_power_init();           /* 各模块清零；此后 SysTick 钩子才真正工作 */

    bsp_iwdg_init(APP_IWDG_TIMEOUT_MS);

    bsp_uart_write("\r\nLeVolt G431 power supply ready\r\n");

    bsp_tim6_start();           /* 10 kHz 快环开始 */
    app_power_run();            /* 不返回 */
}
