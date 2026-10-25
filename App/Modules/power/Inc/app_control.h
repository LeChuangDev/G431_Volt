/**
 * @file  app_control.h —— CV/CC 控制律、dV/dt、软启动、有源下拉、状态机
 */
#ifndef __APP_CONTROL_H
#define __APP_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "app_types.h"

void app_control_init(void);

/* 10 kHz 快环（TIM6 中断里调用，只做整数运算和必要的 DAC 写） */
void app_control_fast(void);

/* 1 kHz 中环（SysTick 中断里调用）：限流仲裁、CV/CC 判定、目标电压 */
void app_control_medium(void);

/* 命令入口（主循环 / 命令解析） */
void app_control_set_voltage(uint16_t mv);
void app_control_set_current(uint16_t ma);
void app_control_request_output(bool on);
void app_control_clear_faults(void);

/* 故障入口（快环/慢环都可以调用，内含立即关断） */
void app_control_fault(uint32_t bits);

/* PD 协商完成 / 输入就绪 */
void app_control_set_input_ready(bool ready);
bool app_control_can_start(void);

pwr_state_t app_control_state(void);

#endif /* __APP_CONTROL_H */
