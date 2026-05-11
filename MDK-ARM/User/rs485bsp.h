#ifndef __RS485BSP_H__
#define __RS485BSP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "app_st25dv.h"
#include <stdint.h>

/*
 * RS485_1 -> USART1 + DE_485_Pin
 * RS485_2 -> USART2 + DE_485_2_Pin
 *
 * TX: queued message DMA. Caller buffer can be released after enqueue.
 * RX: DMA receive-to-idle. Idle/error/TX-complete callbacks drive behavior.
 */

#define RS485_PORT_1                  0u
#define RS485_PORT_2                  1u
#define RS485_PORT_COUNT              2u

#define RS485_FEATURE_NONE                0x00u
#define RS485_FEATURE_JSON_OUT            0x01u  /* Output report frame to upper computer. */
#define RS485_FEATURE_FORWARD_TO_OTHER    0x02u  /* Forward received data to the other 485 port. */
#define RS485_FEATURE_LOG_OUT             0x04u  /* Optional diagnostic output. */
#define RS485_FEATURE_VALID_MASK          (RS485_FEATURE_JSON_OUT | \
                                           RS485_FEATURE_FORWARD_TO_OTHER | \
                                           RS485_FEATURE_LOG_OUT)

#define RS485_REPORT_FRAME_HEAD       0xAAu
#define RS485_REPORT_MAX_SEATS        64u
#define RS485_REPORT_MAX_FRAME_LEN    (2u + (RS485_REPORT_MAX_SEATS * 2u) + 1u)

#ifndef RS485_TX_MAX_MSG
#define RS485_TX_MAX_MSG              16u
#endif

#ifndef RS485_TX_MAX_LEN
#define RS485_TX_MAX_LEN              256u
#endif

#ifndef RS485_RX_DMA_BUF_SIZE
#define RS485_RX_DMA_BUF_SIZE         256u
#endif

typedef struct
{
    uint8_t seat_no;
    uint8_t order_state;
} rs485_seat_report_item_t;

typedef struct
{
    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t rx_frames;
    uint32_t rx_bytes;
    uint32_t forwarded_frames;
    uint32_t forwarded_bytes;
    uint32_t tx_queue_full_count;
    uint32_t tx_start_fail_count;
    uint32_t uart_error_count;
    uint8_t  feature_flags;
    uint8_t  tx_queue_count;
    uint8_t  tx_busy;
} rs485_port_status_t;

void RS485BSP_Init(void);
void RS485BSP_ApplyConfig(const master_payload_t *cfg);
void RS485BSP_Flush(uint8_t port);

void RS485BSP_SetPortFeatures(uint8_t port, uint8_t feature_flags);
uint8_t RS485BSP_GetPortFeatures(uint8_t port);

HAL_StatusTypeDef RS485BSP_SendBuf(uint8_t port, const uint8_t *buf, uint16_t len);
HAL_StatusTypeDef RS485BSP_SendByFeature(uint8_t feature_mask, const uint8_t *buf, uint16_t len);
int float_to_str_2(char *buf, uint16_t buf_size, float value);

/* Upper-computer report frame:
 *   byte0      0xAA
 *   byte1      seat_count
 *   byte2...   seat_no, order_state repeated
 *   last byte  checksum = low 8 bits of the sum of previous bytes
 */
HAL_StatusTypeDef RS485BSP_SendSeatReport(const rs485_seat_report_item_t *seats,
                                          uint8_t seat_count);

/* Convenience interface for master state machine. seat_no == 0 is omitted. */
HAL_StatusTypeDef RS485BSP_SendTwoSeatReport(uint8_t seat0_no,
                                             uint8_t seat0_order_state,
                                             uint8_t seat1_no,
                                             uint8_t seat1_order_state);

void RS485BSP_LogPrintf(const char *fmt, ...);
void RS485BSP_GetPortStatus(uint8_t port, rs485_port_status_t *status);

/* Backward-compatible names kept for existing modules. */
HAL_StatusTypeDef UART1_SendBuf_DMA(uint8_t *buf, uint16_t len);
void UART1_Printf_DMA(const char *fmt, ...);
HAL_StatusTypeDef UART2_SendBuf_DMA(uint8_t *buf, uint16_t len);
void UART2_Printf_DMA(const char *fmt, ...);
void UART_DBG_Printf_DMA(const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __RS485BSP_H__ */
