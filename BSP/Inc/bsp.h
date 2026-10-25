/**
 * @file  bsp.h —— 板级接口（App 层唯一需要 include 的头文件）
 *
 * 分层规则：
 *   App ──► bsp.h ──► BSP/Src 下的驱动 ──► Drivers/External（芯片协议层）──► 寄存器
 * App 不直接包含 stm32g4xx.h，也不操作任何寄存器。
 */
#ifndef __BSP_H
#define __BSP_H

#include <stdint.h>
#include <stdbool.h>
#include "bsp_board.h"

/* =====================================================================
 * 时钟 / 延时 / LED
 * ===================================================================== */
int      bsp_clock_init(void);          /* 0 = 170MHz；非 0 = 已回退 HSI */
void     bsp_delay_init(void);          /* SysTick 1ms + DWT 微秒 */
uint32_t bsp_tick_ms(void);
void     bsp_delay_ms(uint32_t ms);
void     bsp_delay_us(uint32_t us);
void     bsp_tick_1ms_hook(void);       /* 弱符号，App 可覆盖（SysTick 中断里调用） */

void bsp_led_init(void);
void bsp_led_on(void);
void bsp_led_off(void);
void bsp_led_toggle(void);

/* =====================================================================
 * GPIO 控制 / 故障输入
 * ===================================================================== */
void     bsp_gpio_init(void);           /* ★ 上电第一件事：OUT_EN 高、SINK 关 */
void     bsp_out_enable(bool enable);   /* true = 允许源出（PB10 拉低） */
void     bsp_sink_enable(bool enable);  /* true = 打开有源下拉（PB11 拉高） */
bool     bsp_out_is_enabled(void);
bool     bsp_nfault_asserted(void);     /* nFAULT 低有效 */
bool     bsp_hb_in_level(void);
void     bsp_hb_out_set(bool level);
void     bsp_data_ready_set(bool level);
bool     bsp_prot_trip_active(void);    /* 硬件比较器强制关断 */
uint32_t bsp_prot_trip_count(void);     /* 上电以来的 EXTI 次数 */
void     bsp_prot_trip_clear(void);

/* =====================================================================
 * ADC（注入组：ADC1 4 路 + ADC2 3 路）
 * ===================================================================== */
typedef struct {
    uint16_t vout;      /* ADC1 rank1  PA0 */
    uint16_t iout;      /* ADC1 rank2  PA1 */
    uint16_t vin;       /* ADC1 rank3  PA2 */
    uint16_t iin;       /* ADC1 rank4  PA3 */
    uint16_t ntc;       /* ADC2 rank1  PA6 */
    uint16_t ocp;       /* ADC2 rank2  PA7 */
    uint16_t ovp;       /* ADC2 rank3  PB0 */
} bsp_adc_raw_t;

void bsp_adc_init(void);
void bsp_adc_start(void);               /* 启动下一轮注入转换（软件触发） */
void bsp_adc_read(bsp_adc_raw_t *out);  /* 读取上一轮结果（100 µs 前） */
bool bsp_adc_valid(void);               /* 至少完成过一轮 */

/* 板级换算：raw → mV / mA（不含 App 校准系数） */
uint32_t bsp_vout_mv(uint16_t raw);
uint32_t bsp_iout_ma(uint16_t raw);
uint32_t bsp_vin_mv(uint16_t raw);
uint32_t bsp_iin_ma(uint16_t raw);
uint32_t bsp_ntc_mv(uint16_t raw);
bool     bsp_ntc_celsius_x10(uint16_t raw, int16_t *t_x10);  /* false = 断路/短路 */

/* =====================================================================
 * 内部 DAC（PA4 = ICTRL 输入限流控制电压）
 * ===================================================================== */
void bsp_dac_init(void);
void bsp_dac_set_mv(uint32_t mv);       /* 0..3300 mV */

/* =====================================================================
 * 外部 DAC80502（SPI1）
 * ===================================================================== */
void bsp_vdac_init(void);               /* 配置 + 立即写入 0 V 对应码 */
int  bsp_vdac_set_vset_code(uint16_t code);   /* CH0：输出电压设定 */
int  bsp_vdac_set_iset_code(uint16_t code);   /* CH1：输出电流设定 */
int  bsp_vdac_write_all(uint16_t vset_code, uint16_t iset_code);

/* =====================================================================
 * SPI2 从机（向 ESP32 送遥测帧）
 *   G431 不能主动发；ESP32 以 1 kHz 轮询读取。每帧固定 32 字节。
 * ===================================================================== */
void     bsp_spi2_slave_init(void);
void     bsp_spi2_slave_set_frame(const uint8_t *data, uint32_t len);
uint32_t bsp_spi2_slave_frame_count(void);
bool     bsp_spi2_slave_link_ok(void);   /* 最近 100 ms 内被时钟过 */
void     bsp_spi2_slave_poll(void);      /* 主循环调用：长时间无帧时恢复 DMA */

/* =====================================================================
 * I2C1：主（CH224Q）+ 从（ESP32 命令）复用
 * ===================================================================== */
void bsp_i2c1_init(void);
int  bsp_i2c_master_write(uint8_t addr7, const uint8_t *data, uint32_t len);
int  bsp_i2c_master_read(uint8_t addr7, uint8_t *data, uint32_t len);
int  bsp_i2c_master_write_read(uint8_t addr7, const uint8_t *wr, uint32_t wn,
                               uint8_t *rd, uint32_t rn);

/* 从机命令：一整帧在 STOP 时入队；主循环取走 */
bool bsp_i2c_take_command(uint8_t *buf, uint32_t buf_len, uint32_t *out_len);

/* =====================================================================
 * TIM6：10 kHz 快环
 * ===================================================================== */
void     bsp_tim6_init(void);
void     bsp_tim6_start(void);
uint32_t bsp_fast_tick_count(void);
void     bsp_fast_tick_hook(void);      /* 弱符号，App 覆盖（10 kHz 中断里调用） */

/* =====================================================================
 * IWDG / Flash / UART / PD
 * ===================================================================== */
void bsp_iwdg_init(uint32_t timeout_ms);   /* 约 200 ms（LSI 32 kHz） */
void bsp_iwdg_feed(void);

int  bsp_flash_erase_cfg_page(void);
int  bsp_flash_program_cfg(uint32_t offset, const void *data, uint32_t len);

void bsp_uart_init(uint32_t baud);
void bsp_uart_putc(char c);
void bsp_uart_write(const char *s);
void bsp_uart_write_u32(uint32_t v);

/* PD 协商芯片 CH224Q（I2C1） */
void bsp_pd_init(void);
int  bsp_pd_set_voltage(uint8_t volts);      /* 5/9/12/15/20/28 V */
int  bsp_pd_set_current(uint32_t ma);
int  bsp_pd_read_vbus_mv(uint16_t *mv);      /* 读 CH224Q 的 VBUS 寄存器（辅助） */

#endif /* __BSP_H */
