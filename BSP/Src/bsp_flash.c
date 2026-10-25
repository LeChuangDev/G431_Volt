/**
 * @file  bsp_flash.c —— 校准参数区（最后一页，2 KB）
 *
 * G4 单 bank Flash：
 *   - 页大小 2 KB，最后一页 = page 63 @ 0x0801F800
 *   - 编程必须按 64-bit 双字写（先写低 32 位，再写高 32 位）
 *   - 擦除/编程期间 CPU 取指会停住，因此全程关中断
 *
 * 调用者保证 offset/len 均为 8 字节对齐；上层 app_signal 用固定结构体，
 * 结构体本身加了 aligned(8) 并补齐到 8 的整数倍。
 */
#include "bsp.h"
#include "stm32g4xx.h"

#define FLASH_KEY1  0x45670123u
#define FLASH_KEY2  0xCDEF89ABu

#define FLASH_ERR_MASK  (FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                         FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR | \
                         FLASH_SR_FASTERR | FLASH_SR_RDERR | FLASH_SR_OPTVERR)

static void flash_clear_errors(void)
{
    FLASH->SR = FLASH_ERR_MASK | FLASH_SR_EOP;
}

static int flash_unlock(void)
{
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        FLASH->KEYR = FLASH_KEY1;
        FLASH->KEYR = FLASH_KEY2;
    }
    return (FLASH->CR & FLASH_CR_LOCK) != 0u ? -1 : 0;
}

static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

static void flash_wait(void)
{
    while ((FLASH->SR & FLASH_SR_BSY) != 0u) { }
}

int bsp_flash_erase_cfg_page(void)
{
    int rc = 0;

    __disable_irq();
    if (flash_unlock() != 0) {
        __enable_irq();
        return -1;
    }
    flash_wait();
    flash_clear_errors();

    FLASH->CR = (FLASH->CR & ~FLASH_CR_PNB) |
                (BSP_FLASH_CFG_PAGE << FLASH_CR_PNB_Pos) |
                FLASH_CR_PER;
    FLASH->CR |= FLASH_CR_STRT;
    flash_wait();

    FLASH->CR &= ~FLASH_CR_PER;
    if ((FLASH->SR & FLASH_ERR_MASK) != 0u) {
        rc = -2;
    }
    flash_clear_errors();
    flash_lock();
    __enable_irq();
    return rc;
}

int bsp_flash_program_cfg(uint32_t offset, const void *data, uint32_t len)
{
    if (data == 0) {
        return -1;
    }
    if ((offset & 7u) != 0u || (len & 7u) != 0u) {
        return -2;
    }
    if (offset + len > BSP_FLASH_PAGE_SIZE) {
        return -3;
    }

    const uint32_t *src = (const uint32_t *)data;
    uint32_t addr = BSP_FLASH_CFG_ADDR + offset;
    int rc = 0;

    __disable_irq();
    if (flash_unlock() != 0) {
        __enable_irq();
        return -1;
    }
    flash_wait();
    flash_clear_errors();
    FLASH->CR |= FLASH_CR_PG;

    for (uint32_t i = 0; i < len; i += 8u) {
        *(volatile uint32_t *)(addr + i)       = src[i / 4u];
        *(volatile uint32_t *)(addr + i + 4u)  = src[i / 4u + 1u];
        flash_wait();
        if ((FLASH->SR & FLASH_ERR_MASK) != 0u) {
            rc = -4;
            break;
        }
    }

    FLASH->CR &= ~FLASH_CR_PG;
    flash_clear_errors();
    flash_lock();
    __enable_irq();
    return rc;
}
