/**
 * @file  app_power.h —— 顶层调度（快/中/慢环 + 看门狗门控）
 */
#ifndef __APP_POWER_H
#define __APP_POWER_H

void app_power_init(void);
void app_power_run(void);     /* 不返回 */

#endif /* __APP_POWER_H */
