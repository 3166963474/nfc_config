#include "rs485bsp.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* =========================================================
 * DE 控制（高=发送 / 低=接收）
 * 当前工程：UART1 为 RS485
 * DE 引脚命名保持原先不变
 * ========================================================= */
static inline void RS485_DE_TX(void)
{
    HAL_GPIO_WritePin(DE_485_GPIO_Port, DE_485_Pin, GPIO_PIN_SET);
}

static inline void RS485_DE_RX(void)
{
    HAL_GPIO_WritePin(DE_485_GPIO_Port, DE_485_Pin, GPIO_PIN_RESET);
}

/* =========================================================
 * 忙标志
 * - UART1: RS485
 * - UART2: 普通串口
 * ========================================================= */
static volatile uint8_t g_uart1_tx_busy = 0;
static volatile uint8_t g_uart2_tx_busy = 0;

/* Printf 专用静态缓冲区（DMA 必须保证发送期间内存有效） */
static uint8_t g_uart1_printf_buf[256];
static uint8_t g_uart2_printf_buf[256];

/* SendLine 专用静态缓冲区 */
static uint8_t g_uart1_buf_buf[256];
static uint8_t g_uart2_buf_buf[256];

/* =========================================================
 * UART1 (RS485) 直接发送指定 buf + len
 * 注意：
 * 1. buf 在 DMA 发送完成前必须保持有效
 * 2. 发送完成后在 HAL_UART_TxCpltCallback() 中自动拉低 DE
 * ========================================================= */
HAL_StatusTypeDef UART1_SendBuf_DMA(uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef st;

    if (buf == NULL || len == 0)
        return HAL_ERROR;

    while (g_uart1_tx_busy);

    g_uart1_tx_busy = 1;
		memcpy(g_uart1_buf_buf,buf,len);
    /* RS485 切到发送态 */
    RS485_DE_TX();

    /* 清 TC，防止伪完成 */
    __HAL_UART_CLEAR_FLAG(&huart1, UART_FLAG_TC);

    st = HAL_UART_Transmit_DMA(&huart1, g_uart1_buf_buf, len);
    if (st != HAL_OK)
    {
        RS485_DE_RX();
        g_uart1_tx_busy = 0;
    }

    return st;
}

/* =========================================================
 * UART2 (普通串口) 直接发送指定 buf + len
 * 注意：
 * 1. buf 在 DMA 发送完成前必须保持有效
 * ========================================================= */
HAL_StatusTypeDef UART2_SendBuf_DMA(uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef st;

    if (buf == NULL || len == 0)
        return HAL_ERROR;

    while (g_uart2_tx_busy);

    g_uart2_tx_busy = 1;
		memcpy(g_uart2_buf_buf,buf,len);
    st = HAL_UART_Transmit_DMA(&huart2, g_uart2_buf_buf, len);
    if (st != HAL_OK)
    {
        g_uart2_tx_busy = 0;
    }

    return st;
}

/* =========================================================
 * UART1 Printf_DMA
 * 自动格式化并追加 \r\n
 * ========================================================= */
void UART1_Printf_DMA(const char *fmt, ...)
{
    va_list args;
    int len;

    if (fmt == NULL)
        return;

    /* 忙则等待，也可以改成直接 return */
    while (g_uart1_tx_busy);

    va_start(args, fmt);
    len = vsnprintf((char *)g_uart1_printf_buf,
                    sizeof(g_uart1_printf_buf) - 2,
                    fmt,
                    args);
    va_end(args);

    if (len <= 0)
        return;

    if (len > (int)(sizeof(g_uart1_printf_buf) - 2))
        len = sizeof(g_uart1_printf_buf) - 2;

    (void)UART1_SendBuf_DMA(g_uart1_printf_buf, (uint16_t)len);
}

/* =========================================================
 * UART2 Printf_DMA
 * 自动格式化并追加 \r\n
 * ========================================================= */
void UART2_Printf_DMA(const char *fmt, ...)
{
    va_list args;
    int len;

    if (fmt == NULL)
        return;

    /* 忙则等待，也可以改成直接 return */
    while (g_uart2_tx_busy);

    va_start(args, fmt);
    len = vsnprintf((char *)g_uart2_printf_buf,
                    sizeof(g_uart2_printf_buf) - 2,
                    fmt,
                    args);
    va_end(args);

    if (len <= 0)
        return;

    if (len > (int)(sizeof(g_uart2_printf_buf) - 2))
        len = sizeof(g_uart2_printf_buf) - 2;

    (void)UART2_SendBuf_DMA(g_uart2_printf_buf, (uint16_t)len);
}

/* =========================================================
 * DMA发送完成回调
 * UART1: RS485 需要拉低 DE
 * UART2: 普通串口只清 busy
 * ========================================================= */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        RS485_DE_RX();
        g_uart1_tx_busy = 0;
    }
    else if (huart->Instance == USART2)
    {
        g_uart2_tx_busy = 0;
    }
}

/* =========================================================
 * 出错回调
 * 防止 busy 或 DE 卡死
 * ========================================================= */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        RS485_DE_RX();
        g_uart1_tx_busy = 0;
    }
    else if (huart->Instance == USART2)
    {
        g_uart2_tx_busy = 0;
    }
}

#define UART_DBG_TX_BUF_SIZE   256

static uint8_t uart_dbg_need_send_uart1(void)
{
    uint32_t *obj = payload_GetObject();
    if (obj == NULL)
    {
        return 0;
    }

    /* payload_GetObject 返回的对象起始处就是 rf_param，
       reserved 紧跟其后，所以统一按字节访问 reserved[0] */
    uint8_t *p = (uint8_t *)obj;

    /* reserved 位于 rf_param 之后 */
    uint8_t reserved0 = p[sizeof(rf_param_t)];

    return ((reserved0 & UART1_MIRROR_EN_BIT) != 0U) ? 1U : 0U;
}

void UART_DBG_Printf_DMA(const char *fmt, ...)
{
    static char tx_buf[UART_DBG_TX_BUF_SIZE];
    uint32_t tick;
    int prefix_len;
    int body_len;
    va_list args;

    if (fmt == NULL)
    {
        return;
    }

    tick = HAL_GetTick();

    prefix_len = snprintf(tx_buf, sizeof(tx_buf), "[%lu] ", (unsigned long)tick);
    if (prefix_len < 0 || prefix_len >= (int)sizeof(tx_buf))
    {
        return;
    }

    va_start(args, fmt);
    body_len = vsnprintf(&tx_buf[prefix_len],
                         sizeof(tx_buf) - (uint32_t)prefix_len,
                         fmt,
                         args);
    va_end(args);

    if (body_len < 0)
    {
        return;
    }

    if ((prefix_len + body_len) >= (int)sizeof(tx_buf))
    {
        return;
    }

    /* 先发 UART2 */
    (void)UART2_SendBuf_DMA((uint8_t *)tx_buf, (uint16_t)(prefix_len + body_len));

    /* 配置允许时，同时镜像到 UART1 */
    if (uart_dbg_need_send_uart1())
    {
        (void)UART1_SendBuf_DMA((uint8_t *)tx_buf, (uint16_t)(prefix_len + body_len));
    }
}
