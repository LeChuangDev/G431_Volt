/**
 * @file  app_comms.h —— SPI2 遥测 / 波形 + I2C 命令 + 双向心跳
 */
#ifndef __APP_COMMS_H
#define __APP_COMMS_H

#include <stdint.h>
#include <stdbool.h>

void app_comms_init(void);

/* 10 kHz 快拍里采样波形（只做装填，不发帧） */
void app_comms_sample(uint16_t vout_mv, uint16_t iout_ma);

/* 1 kHz：装帧、心跳、DATA_RDY */
void app_comms_medium(uint32_t now_ms);

/* 主循环：取走并执行 ESP32 命令（最多处理若干条） */
void app_comms_poll(void);

bool app_comms_wireless_alive(void);

/* ---- 命令格式（I2C 从机 0x24，主机写）----
 *   [0]=0x5A  [1]=opcode  [2]=payload_len  [3..]=payload  [crc16 lo, hi]
 *   CRC16-CCITT(0x1021, init 0xFFFF)，覆盖 [0 .. 2+len)
 */
#define APP_CMD_SET_VOLTAGE     0x01u   /* u16 mV */
#define APP_CMD_SET_CURRENT     0x02u   /* u16 mA */
#define APP_CMD_OUTPUT_ON       0x03u   /* - */
#define APP_CMD_OUTPUT_OFF      0x04u   /* - */
#define APP_CMD_SCREEN_STATE    0x05u   /* u8 0=灭 1=亮 */
#define APP_CMD_CLEAR_FAULT     0x06u   /* - */
#define APP_CMD_CAL_SET_K       0x10u   /* u8 k + i32 value */
#define APP_CMD_CAL_SET_VPOINT  0x11u   /* u8 idx + u16 raw + i16 mV */
#define APP_CMD_CAL_SET_IPOINT  0x12u   /* u8 idx + u16 raw + i16 mA */
#define APP_CMD_CAL_SAVE        0x13u   /* - */
#define APP_CMD_CAL_DEFAULTS    0x14u   /* - */
#define APP_CMD_CAL_GET_K       0x15u   /* u8 k —— 结果走 UART 日志 */

#endif /* __APP_COMMS_H */
