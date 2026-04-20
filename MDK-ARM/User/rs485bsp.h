#ifndef __RS485BSP_H__
#define __RS485BSP_H__

#include "main.h"
#include <stdint.h>
#include "app_st25dv.h"
/* =========================
 * UART1 -> RS485
 * UART2 -> 普通串口
 * 都使用 DMA 发送
 * ========================= */

/* UART1 (RS485) */
HAL_StatusTypeDef UART1_SendBuf_DMA(uint8_t *buf, uint16_t len);
void UART1_Printf_DMA(const char *fmt, ...);

/* UART2 (普通串口) */
HAL_StatusTypeDef UART2_SendBuf_DMA(uint8_t *buf, uint16_t len);
void UART2_Printf_DMA(const char *fmt, ...);

void UART_DBG_Printf_DMA(const char *fmt, ...);

#define UART1_MIRROR_EN_BIT    (1U << 1)

#endif
