/**
 * @file  app_pd.h —— CH224Q USB-PD 协商（100 ms 慢环调用）
 */
#ifndef __APP_PD_H
#define __APP_PD_H

#include <stdint.h>
#include <stdbool.h>

void app_pd_init(void);

/* 非阻塞状态机；每 100 ms 调一次 */
void app_pd_task(void);

/* 输入是否已确认可用（VIN 达到协商值） */
bool app_pd_input_ready(void);

/* 协商到的源电压（V），0 = 未知 */
uint8_t app_pd_voltage(void);

#endif /* __APP_PD_H */
