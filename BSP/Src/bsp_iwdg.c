/**
 * @file  bsp_iwdg.c —— 独立看门狗（LSI 32 kHz，约 200 ms）
 *
 * 只在主循环喂狗（且要求快环确实在跑），SysTick 里绝不喂——否则主循环
 * 卡死时看门狗就失去意义。
 */
#include "bsp.h"
#include "stm32g4xx.h"

void bsp_iwdg_init(uint32_t timeout_ms)
{
    /* LSI ≈ 32 kHz。PR=3 → /32 → 1 kHz；RLR = timeout_ms - 1 */
    uint32_t reload = timeout_ms;
    if (reload > 4095u) {
        reload = 4095u;
    }
    if (reload == 0u) {
        reload = 1u;
    }

    IWDG->KR = 0xCCCCu;             /* 启动 */
    IWDG->KR = 0x5555u;             /* 解锁 PR/RLR */
    IWDG->PR = 3u;                  /* /32 */
    IWDG->RLR = reload;
    while ((IWDG->SR & 0x03u) != 0u) { }   /* 等 PVU/RVU 更新完成 */
    IWDG->KR = 0xAAAAu;             /* 首次喂狗 */
}

void bsp_iwdg_feed(void)
{
    IWDG->KR = 0xAAAAu;
}
