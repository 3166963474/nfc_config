#include "rs485bsp.h"
#include "usart.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* =========================================================
 * UART DMA queue item, similar to i2c1_dma_queue.c:
 * - each enqueued message owns a static buffer
 * - DMA sends one queued item at a time
 * - TX complete/error callback pops current item and starts next
 * ========================================================= */

typedef struct
{
    uint16_t len;
    uint8_t  buf[RS485_TX_MAX_LEN];
} rs485_tx_item_t;

typedef struct
{
    UART_HandleTypeDef *huart;
    GPIO_TypeDef *de_port;
    uint16_t de_pin;

    uint8_t feature_flags;

    rs485_tx_item_t tx_q[RS485_TX_MAX_MSG];
    volatile uint8_t tx_head;
    volatile uint8_t tx_tail;
    volatile uint8_t tx_count;
    volatile uint8_t tx_sending;

    uint8_t rx_dma_buf[RS485_RX_DMA_BUF_SIZE];

    uint32_t tx_frames;
    uint32_t tx_bytes;
    uint32_t rx_frames;
    uint32_t rx_bytes;
    uint32_t forwarded_frames;
    uint32_t forwarded_bytes;
    uint32_t tx_queue_full_count;
    uint32_t tx_start_fail_count;
    uint32_t uart_error_count;
} rs485_port_ctx_t;

rs485_port_ctx_t s_rs485[RS485_PORT_COUNT] = {
    {
        .huart = &huart1,
        .de_port = DE_485_GPIO_Port,
        .de_pin = DE_485_Pin,
        .feature_flags = RS485_FEATURE_NONE,
    },
    {
        .huart = &huart2,
        .de_port = DE_485_2_GPIO_Port,
        .de_pin = DE_485_2_Pin,
        .feature_flags = RS485_FEATURE_NONE,
    },
};

static const uint32_t s_uart_baudrate_table[] = {
    300UL,
    600UL,
    1200UL,
    2400UL,
    4800UL,
    9600UL,
    14400UL,
    19200UL,
    28800UL,
    38400UL,
    57600UL,
    115200UL,
    230400UL,
    460800UL,
    921600UL,
};

#define RS485_UART_BAUDRATE_COUNT \
    ((uint8_t)(sizeof(s_uart_baudrate_table) / sizeof(s_uart_baudrate_table[0])))

#ifndef RS485_DEFAULT_BAUDRATE_INDEX
#define RS485_DEFAULT_BAUDRATE_INDEX 11u /* 115200 */
#endif

#ifndef RS485_ENABLE_RUNTIME_BAUDRATE_CONFIG
#define RS485_ENABLE_RUNTIME_BAUDRATE_CONFIG 1u
#endif

#define RS485_PRINTF_BUF_SIZE 256u
#define RS485_LOG_BUF_SIZE    256u

static inline void q_lock(void)
{
    __disable_irq();
}

static inline void q_unlock(void)
{
    __enable_irq();
}

static inline void rs485_de_tx(const rs485_port_ctx_t *p)
{
    HAL_GPIO_WritePin(p->de_port, p->de_pin, GPIO_PIN_SET);
}

static inline void rs485_de_rx(const rs485_port_ctx_t *p)
{
    HAL_GPIO_WritePin(p->de_port, p->de_pin, GPIO_PIN_RESET);
}

static uint8_t rs485_port_is_valid(uint8_t port)
{
    return (port < RS485_PORT_COUNT) ? 1u : 0u;
}

static rs485_port_ctx_t *rs485_get_by_uart(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
    {
        return NULL;
    }

    if (huart->Instance == USART1)
    {
        return &s_rs485[RS485_PORT_1];
    }

    if (huart->Instance == USART2)
    {
        return &s_rs485[RS485_PORT_2];
    }

    return NULL;
}

static uint8_t rs485_ctx_to_port(const rs485_port_ctx_t *p)
{
    return (p == &s_rs485[RS485_PORT_1]) ? RS485_PORT_1 : RS485_PORT_2;
}

static uint8_t rs485_other_port(uint8_t port)
{
    return (port == RS485_PORT_1) ? RS485_PORT_2 : RS485_PORT_1;
}

static void rs485_start_rx_dma_idle(rs485_port_ctx_t *p)
{
    if ((p == NULL) || (p->huart == NULL))
    {
        return;
    }

    (void)HAL_UARTEx_ReceiveToIdle_DMA(p->huart,
                                       p->rx_dma_buf,
                                       (uint16_t)sizeof(p->rx_dma_buf));

    /* Reduce callback load: we only need idle/full events, not half-transfer. */
    if (p->huart->hdmarx != NULL)
    {
        __HAL_DMA_DISABLE_IT(p->huart->hdmarx, DMA_IT_HT);
    }
}

static void rs485_pop_finished_unsafe(rs485_port_ctx_t *p)
{
    if (p->tx_count > 0u)
    {
        p->tx_tail = (uint8_t)((p->tx_tail + 1u) % RS485_TX_MAX_MSG);
        p->tx_count--;
    }
}

static void rs485_start_next_if_idle(rs485_port_ctx_t *p)
{
    rs485_tx_item_t *it;
    HAL_StatusTypeDef st;

    if (p == NULL)
    {
        return;
    }

    q_lock();

    if ((p->tx_sending != 0u) || (p->tx_count == 0u))
    {
        q_unlock();
        return;
    }

    it = &p->tx_q[p->tx_tail];
    p->tx_sending = 1u;

    q_unlock();

    rs485_de_tx(p);
    __HAL_UART_CLEAR_FLAG(p->huart, UART_FLAG_TC);

    st = HAL_UART_Transmit_DMA(p->huart, it->buf, it->len);
    if (st != HAL_OK)
    {
        q_lock();
        p->tx_start_fail_count++;
        p->tx_sending = 0u;
        rs485_pop_finished_unsafe(p); /* discard the failed item to avoid queue lockup */
        q_unlock();

        rs485_de_rx(p);
        rs485_start_next_if_idle(p);
    }
}

static HAL_StatusTypeDef rs485_queue_tx(rs485_port_ctx_t *p,
                                        const uint8_t *data,
                                        uint16_t len)
{
    rs485_tx_item_t *it;
    uint8_t need_start;

    if ((p == NULL) || (data == NULL) || (len == 0u) || (len > RS485_TX_MAX_LEN))
    {
        return HAL_ERROR;
    }

    q_lock();

    if (p->tx_count >= RS485_TX_MAX_MSG)
    {
        p->tx_queue_full_count++;
        q_unlock();
        return HAL_BUSY;
    }

    it = &p->tx_q[p->tx_head];
    it->len = len;
    (void)memcpy(it->buf, data, len);

    p->tx_head = (uint8_t)((p->tx_head + 1u) % RS485_TX_MAX_MSG);
    p->tx_count++;

    need_start = (p->tx_sending == 0u) ? 1u : 0u;

    q_unlock();

    if (need_start != 0u)
    {
        rs485_start_next_if_idle(p);
    }

    return HAL_OK;
}

static uint8_t rs485_sum8(const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint8_t s = 0u;

    if (buf == NULL)
    {
        return 0u;
    }

    for (i = 0u; i < len; i++)
    {
        s = (uint8_t)(s + buf[i]);
    }

    return s;
}

#if (RS485_ENABLE_RUNTIME_BAUDRATE_CONFIG != 0u)
static uint32_t rs485_baudrate_from_index(uint8_t baudrate_index)
{
    if (baudrate_index >= RS485_UART_BAUDRATE_COUNT)
    {
        baudrate_index = RS485_DEFAULT_BAUDRATE_INDEX;
    }

    return s_uart_baudrate_table[baudrate_index];
}

static void rs485_apply_baudrate(rs485_port_ctx_t *p, uint8_t baudrate_index)
{
    uint32_t baudrate;

    if ((p == NULL) || (p->huart == NULL))
    {
        return;
    }

    baudrate = rs485_baudrate_from_index(baudrate_index);

    (void)HAL_UART_Abort(p->huart);

    q_lock();
    p->tx_head = 0u;
    p->tx_tail = 0u;
    p->tx_count = 0u;
    p->tx_sending = 0u;
    q_unlock();

    rs485_de_rx(p);

    (void)HAL_UART_DeInit(p->huart);
    p->huart->Init.BaudRate = baudrate;
    (void)HAL_UART_Init(p->huart);

    rs485_start_rx_dma_idle(p);
}
#endif

void RS485BSP_ApplyConfig(const master_payload_t *cfg)
{
    if (cfg == NULL)
    {
        return;
    }
		if(BUS_MASTER_DEF != 1)
		{
			RS485BSP_SetPortFeatures(RS485_PORT_1,
															 (uint8_t)(RS485_FEATURE_NONE & RS485_FEATURE_VALID_MASK));
			RS485BSP_SetPortFeatures(RS485_PORT_2,
															 (uint8_t)(RS485_FEATURE_LOG_OUT & RS485_FEATURE_VALID_MASK));
			return;
		}

    RS485BSP_SetPortFeatures(RS485_PORT_1,
                             (uint8_t)(cfg->rs485_1.feature_flags & RS485_FEATURE_VALID_MASK));
    RS485BSP_SetPortFeatures(RS485_PORT_2,
                             (uint8_t)(cfg->rs485_2.feature_flags & RS485_FEATURE_VALID_MASK));

#if (RS485_ENABLE_RUNTIME_BAUDRATE_CONFIG != 0u)
    rs485_apply_baudrate(&s_rs485[RS485_PORT_1], cfg->rs485_1.baudrate_index);
    rs485_apply_baudrate(&s_rs485[RS485_PORT_2], cfg->rs485_2.baudrate_index);
#endif
}

void RS485BSP_Init(void)
{
    uint8_t i;

    for (i = 0u; i < RS485_PORT_COUNT; i++)
    {
        (void)HAL_UART_Abort(s_rs485[i].huart);

        q_lock();
        s_rs485[i].tx_head = 0u;
        s_rs485[i].tx_tail = 0u;
        s_rs485[i].tx_count = 0u;
        s_rs485[i].tx_sending = 0u;
        q_unlock();

        s_rs485[i].tx_frames = 0ul;
        s_rs485[i].tx_bytes = 0ul;
        s_rs485[i].rx_frames = 0ul;
        s_rs485[i].rx_bytes = 0ul;
        s_rs485[i].forwarded_frames = 0ul;
        s_rs485[i].forwarded_bytes = 0ul;
        s_rs485[i].tx_queue_full_count = 0ul;
        s_rs485[i].tx_start_fail_count = 0ul;
        s_rs485[i].uart_error_count = 0ul;

        rs485_de_rx(&s_rs485[i]);
        rs485_start_rx_dma_idle(&s_rs485[i]);
    }

    RS485BSP_ApplyConfig(&master_payload);
}

void RS485BSP_Flush(uint8_t port)
{
    if (rs485_port_is_valid(port) == 0u)
    {
        return;
    }

    q_lock();
    s_rs485[port].tx_head = 0u;
    s_rs485[port].tx_tail = 0u;
    s_rs485[port].tx_count = 0u;
    s_rs485[port].tx_sending = 0u;
    q_unlock();

    (void)HAL_UART_AbortTransmit(s_rs485[port].huart);
    rs485_de_rx(&s_rs485[port]);
}

void RS485BSP_SetPortFeatures(uint8_t port, uint8_t feature_flags)
{
    if (rs485_port_is_valid(port) == 0u)
    {
        return;
    }

    s_rs485[port].feature_flags = (uint8_t)(feature_flags & RS485_FEATURE_VALID_MASK);
}

uint8_t RS485BSP_GetPortFeatures(uint8_t port)
{
    if (rs485_port_is_valid(port) == 0u)
    {
        return RS485_FEATURE_NONE;
    }

    return s_rs485[port].feature_flags;
}

HAL_StatusTypeDef RS485BSP_SendBuf(uint8_t port, const uint8_t *buf, uint16_t len)
{
    if (rs485_port_is_valid(port) == 0u)
    {
        return HAL_ERROR;
    }

    return rs485_queue_tx(&s_rs485[port], buf, len);
}

HAL_StatusTypeDef RS485BSP_SendByFeature(uint8_t feature_mask, const uint8_t *buf, uint16_t len)
{
    HAL_StatusTypeDef ret = HAL_OK;
    HAL_StatusTypeDef st;
    uint8_t sent = 0u;
    uint8_t i;

    if ((buf == NULL) || (len == 0u))
    {
        return HAL_ERROR;
    }

    for (i = 0u; i < RS485_PORT_COUNT; i++)
    {
        if ((s_rs485[i].feature_flags & feature_mask) != 0u)
        {
            st = rs485_queue_tx(&s_rs485[i], buf, len);
            sent = 1u;
            if (st != HAL_OK)
            {
                ret = st;
            }
        }
    }

    return (sent != 0u) ? ret : HAL_OK;
}

HAL_StatusTypeDef RS485BSP_SendSeatReport(const rs485_seat_report_item_t *seats,
                                          uint8_t seat_count)
{
    uint8_t frame[RS485_REPORT_MAX_FRAME_LEN];
    uint16_t len;
    uint8_t i;

    if ((seats == NULL) || (seat_count == 0u) || (seat_count > RS485_REPORT_MAX_SEATS))
    {
        return HAL_ERROR;
    }

    frame[0] = RS485_REPORT_FRAME_HEAD;
    frame[1] = seat_count;

    for (i = 0u; i < seat_count; i++)
    {
        frame[2u + ((uint16_t)i * 2u)] = seats[i].seat_no;
        frame[3u + ((uint16_t)i * 2u)] = seats[i].order_state;
    }

    len = (uint16_t)(2u + ((uint16_t)seat_count * 2u));
    frame[len] = rs485_sum8(frame, len);
    len++;

    return RS485BSP_SendByFeature(RS485_FEATURE_JSON_OUT, frame, len);
}

HAL_StatusTypeDef RS485BSP_SendTwoSeatReport(uint8_t seat0_no,
                                             uint8_t seat0_order_state,
                                             uint8_t seat1_no,
                                             uint8_t seat1_order_state)
{
    rs485_seat_report_item_t seats[2];
    uint8_t count = 0u;

    if (seat0_no != 0u)
    {
        seats[count].seat_no = seat0_no;
        seats[count].order_state = seat0_order_state;
        count++;
    }

    if (seat1_no != 0u)
    {
        seats[count].seat_no = seat1_no;
        seats[count].order_state = seat1_order_state;
        count++;
    }

    if (count == 0u)
    {
        return HAL_OK;
    }

    return RS485BSP_SendSeatReport(seats, count);
}

void RS485BSP_LogPrintf(const char *fmt, ...)
{
    char buf[RS485_LOG_BUF_SIZE];
    uint32_t tick;
    int prefix_len;
    int body_len;
    va_list args;

    if (fmt == NULL)
    {
        return;
    }

    tick = HAL_GetTick();
    prefix_len = snprintf(buf, sizeof(buf), "[%lu] ", (unsigned long)tick);
    if ((prefix_len < 0) || (prefix_len >= (int)sizeof(buf)))
    {
        return;
    }

    va_start(args, fmt);
    body_len = vsnprintf(&buf[prefix_len],
                         sizeof(buf) - (uint32_t)prefix_len,
                         fmt,
                         args);
    va_end(args);

    if (body_len < 0)
    {
        return;
    }

    if ((prefix_len + body_len) >= (int)sizeof(buf))
    {
        return;
    }

    (void)RS485BSP_SendByFeature(RS485_FEATURE_LOG_OUT,
                                 (const uint8_t *)buf,
                                 (uint16_t)(prefix_len + body_len));
}

void RS485BSP_GetPortStatus(uint8_t port, rs485_port_status_t *status)
{
    rs485_port_ctx_t *p;

    if ((rs485_port_is_valid(port) == 0u) || (status == NULL))
    {
        return;
    }

    p = &s_rs485[port];

    q_lock();
    status->tx_frames = p->tx_frames;
    status->tx_bytes = p->tx_bytes;
    status->rx_frames = p->rx_frames;
    status->rx_bytes = p->rx_bytes;
    status->forwarded_frames = p->forwarded_frames;
    status->forwarded_bytes = p->forwarded_bytes;
    status->tx_queue_full_count = p->tx_queue_full_count;
    status->tx_start_fail_count = p->tx_start_fail_count;
    status->uart_error_count = p->uart_error_count;
    status->feature_flags = p->feature_flags;
    status->tx_queue_count = p->tx_count;
    status->tx_busy = p->tx_sending;
    q_unlock();
}

HAL_StatusTypeDef UART1_SendBuf_DMA(uint8_t *buf, uint16_t len)
{
    return RS485BSP_SendBuf(RS485_PORT_1, buf, len);
}

HAL_StatusTypeDef UART2_SendBuf_DMA(uint8_t *buf, uint16_t len)
{
    return RS485BSP_SendBuf(RS485_PORT_2, buf, len);
}

static void rs485_port_printf(uint8_t port, const char *fmt, va_list args)
{
    char buf[RS485_PRINTF_BUF_SIZE];
    int len;

    if (fmt == NULL)
    {
        return;
    }

    len = vsnprintf(buf, sizeof(buf), fmt, args);
    if ((len <= 0) || (len >= (int)sizeof(buf)))
    {
        return;
    }

    (void)RS485BSP_SendBuf(port, (const uint8_t *)buf, (uint16_t)len);
}

void UART1_Printf_DMA(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    rs485_port_printf(RS485_PORT_1, fmt, args);
    va_end(args);
}

void UART2_Printf_DMA(const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    rs485_port_printf(RS485_PORT_2, fmt, args);
    va_end(args);
}

void UART_DBG_Printf_DMA(const char *fmt, ...)
{
    char buf[RS485_LOG_BUF_SIZE];
    uint32_t tick;
    int prefix_len;
    int body_len;
    va_list args;

    if (fmt == NULL)
    {
        return;
    }

    tick = HAL_GetTick();
    prefix_len = snprintf(buf, sizeof(buf), "[%lu] ", (unsigned long)tick);
    if ((prefix_len < 0) || (prefix_len >= (int)sizeof(buf)))
    {
        return;
    }

    va_start(args, fmt);
    body_len = vsnprintf(&buf[prefix_len],
                         sizeof(buf) - (uint32_t)prefix_len,
                         fmt,
                         args);
    va_end(args);

    if (body_len < 0)
    {
        return;
    }

    if ((prefix_len + body_len) >= (int)sizeof(buf))
    {
        return;
    }

    (void)RS485BSP_SendByFeature(RS485_FEATURE_LOG_OUT,
                                 (const uint8_t *)buf,
                                 (uint16_t)(prefix_len + body_len));
}

/* =========================================================
 * HAL callbacks: TX complete, RX idle event, error
 * ========================================================= */

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    rs485_port_ctx_t *p = rs485_get_by_uart(huart);

    if (p == NULL)
    {
        return;
    }

    q_lock();

    if ((p->tx_sending != 0u) && (p->tx_count > 0u))
    {
        p->tx_frames++;
        p->tx_bytes += p->tx_q[p->tx_tail].len;
        rs485_pop_finished_unsafe(p);
    }

    p->tx_sending = 0u;

    q_unlock();

    if (p->tx_count == 0u)
    {
        rs485_de_rx(p);
    }

    rs485_start_next_if_idle(p);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    rs485_port_ctx_t *src = rs485_get_by_uart(huart);
    uint8_t src_port;
    uint8_t dst_port;
    HAL_StatusTypeDef st;

    if (src == NULL)
    {
        return;
    }

    if ((Size > 0u) && (Size <= RS485_RX_DMA_BUF_SIZE))
    {
        src->rx_frames++;
        src->rx_bytes += Size;

        if ((src->feature_flags & RS485_FEATURE_FORWARD_TO_OTHER) != 0u)
        {
            src_port = rs485_ctx_to_port(src);
            dst_port = rs485_other_port(src_port);
            st = RS485BSP_SendBuf(dst_port, src->rx_dma_buf, Size);
            if (st == HAL_OK)
            {
                src->forwarded_frames++;
                src->forwarded_bytes += Size;
            }
        }
    }

    rs485_start_rx_dma_idle(src);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    rs485_port_ctx_t *p = rs485_get_by_uart(huart);

    if (p == NULL)
    {
        return;
    }

    p->uart_error_count++;

    (void)HAL_UART_Abort(p->huart);

    q_lock();

    if ((p->tx_sending != 0u) && (p->tx_count > 0u))
    {
        rs485_pop_finished_unsafe(p); /* discard failed TX item */
    }

    p->tx_sending = 0u;

    q_unlock();

    rs485_de_rx(p);
    rs485_start_rx_dma_idle(p);
    rs485_start_next_if_idle(p);
}
#include <stdint.h>

int float_to_str_2(char *buf, uint16_t buf_size, float value)
{
    char tmp[12];
    uint8_t tmp_len = 0;
    uint8_t pos = 0;
    uint32_t scaled;
    uint32_t int_part;
    uint32_t frac_part;
    char sign;

    if (buf == 0 || buf_size < 6)
    {
        return -1;
    }

    if (value < 0.0f)
    {
        sign = '-';
        value = -value;
    }
    else
    {
        sign = '+';
    }

    /* 放大100倍并四舍五入 */
    scaled = (uint32_t)(value * 100.0f + 0.5f);

    int_part = scaled / 100U;
    frac_part = scaled % 100U;

    buf[pos++] = sign;

    /* 整数部分转字符串 */
    if (int_part == 0U)
    {
        if (pos >= buf_size - 1U) return -1;
        buf[pos++] = '0';
    }
    else
    {
        while (int_part > 0U && tmp_len < sizeof(tmp))
        {
            tmp[tmp_len++] = (char)('0' + (int_part % 10U));
            int_part /= 10U;
        }

        while (tmp_len > 0U)
        {
            if (pos >= buf_size - 1U) return -1;
            buf[pos++] = tmp[--tmp_len];
        }
    }

    if (pos >= buf_size - 1U) return -1;
    buf[pos++] = '.';

    if (pos >= buf_size - 1U) return -1;
    buf[pos++] = (char)('0' + (frac_part / 10U));

    if (pos >= buf_size - 1U) return -1;
    buf[pos++] = (char)('0' + (frac_part % 10U));

    if (pos >= buf_size) return -1;
    buf[pos] = '\0';

    return (int)pos;
}