#include "infinite_protocol.h"

#include <stddef.h>

void UART_DBG_Printf_DMA(const char *fmt, ...);

uint8_t InfiniteProtocol_CalcChecksum(const uint8_t *buf, uint16_t len_without_checksum)
{
    uint32_t sum = 0u;
    uint16_t i;

    if (buf == NULL)
    {
        return 0u;
    }

    for (i = 0u; i < len_without_checksum; i++)
    {
        sum += buf[i];
    }

    return (uint8_t)(sum & 0xFFu);
}

static infinite_protocol_status_t InfiniteProtocol_Dispatch(
    uint8_t func,
    const uint8_t *payload,
    uint8_t payload_len,
    const infinite_protocol_handler_entry_t *table,
    uint16_t table_size)
{
    uint16_t i;

    if ((table == NULL) || (table_size == 0u))
    {
        UART_DBG_Printf_DMA("[PROTO] no handler table\r\n");
        return INFINITE_PROTOCOL_ERR_FUNC;
    }

    for (i = 0u; i < table_size; i++)
    {
        if ((table[i].func == func) && (table[i].handler != NULL))
        {
            table[i].handler(payload, payload_len);
            return INFINITE_PROTOCOL_OK;
        }
    }

    UART_DBG_Printf_DMA("[PROTO] unknown func: 0x%02X\r\n", func);
    return INFINITE_PROTOCOL_ERR_FUNC;
}

infinite_protocol_status_t InfiniteProtocol_UnpackAndDispatch(
    const uint8_t *rx_buf,
    uint16_t rx_len,
    const infinite_protocol_handler_entry_t *table,
    uint16_t table_size)
{
    uint8_t func;
    uint8_t payload_len;
    uint8_t recv_checksum;
    uint8_t calc_checksum;
    uint16_t expected_len;
    const uint8_t *payload;

    if (rx_buf == NULL)
    {
        return INFINITE_PROTOCOL_ERR_NULL;
    }

    if (rx_len < INFINITE_PROTOCOL_MIN_FRAME_LEN)
    {
        UART_DBG_Printf_DMA("[PROTO] len too short: %u\r\n", rx_len);
        return INFINITE_PROTOCOL_ERR_LEN;
    }

    if (rx_buf[INFINITE_PROTOCOL_OFFSET_HEADER] != INFINITE_PROTOCOL_HEADER)
    {
        UART_DBG_Printf_DMA("[PROTO] header error: 0x%02X\r\n",
                            rx_buf[INFINITE_PROTOCOL_OFFSET_HEADER]);
        return INFINITE_PROTOCOL_ERR_HEADER;
    }

    func = rx_buf[INFINITE_PROTOCOL_OFFSET_FUNC];
    payload_len = rx_buf[INFINITE_PROTOCOL_OFFSET_LEN];

    if (payload_len > INFINITE_PROTOCOL_MAX_PAYLOAD_LEN)
    {
        UART_DBG_Printf_DMA("[PROTO] payload too long: %u\r\n", payload_len);
        return INFINITE_PROTOCOL_ERR_LEN;
    }

    expected_len = (uint16_t)payload_len + INFINITE_PROTOCOL_MIN_FRAME_LEN;
    if (rx_len != expected_len)
    {
        UART_DBG_Printf_DMA("[PROTO] len mismatch, rx=%u expected=%u\r\n",
                            rx_len,
                            expected_len);
        return INFINITE_PROTOCOL_ERR_LEN;
    }

    recv_checksum = rx_buf[expected_len - 1u];
    calc_checksum = InfiniteProtocol_CalcChecksum(rx_buf, (uint16_t)(expected_len - 1u));
    if (recv_checksum != calc_checksum)
    {
        UART_DBG_Printf_DMA("[PROTO] checksum error, recv=0x%02X calc=0x%02X\r\n",
                            recv_checksum,
                            calc_checksum);
        return INFINITE_PROTOCOL_ERR_CHECKSUM;
    }

    payload = &rx_buf[INFINITE_PROTOCOL_OFFSET_PAYLOAD];
    return InfiniteProtocol_Dispatch(func, payload, payload_len, table, table_size);
}
