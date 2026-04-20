#ifndef INFINITE_PROTOCOL_H
#define INFINITE_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INFINITE_PROTOCOL_HEADER           0xFCu

#define INFINITE_PROTOCOL_OFFSET_HEADER    0u
#define INFINITE_PROTOCOL_OFFSET_FUNC      1u
#define INFINITE_PROTOCOL_OFFSET_LEN       2u
#define INFINITE_PROTOCOL_OFFSET_PAYLOAD   3u

/* header + func + len + checksum */
#define INFINITE_PROTOCOL_MIN_FRAME_LEN    4u
#define INFINITE_PROTOCOL_MAX_PAYLOAD_LEN  64u

typedef enum
{
    INFINITE_PROTOCOL_OK = 0,
    INFINITE_PROTOCOL_ERR_NULL,
    INFINITE_PROTOCOL_ERR_LEN,
    INFINITE_PROTOCOL_ERR_HEADER,
    INFINITE_PROTOCOL_ERR_CHECKSUM,
    INFINITE_PROTOCOL_ERR_FUNC
} infinite_protocol_status_t;

typedef void (*infinite_protocol_handler_t)(const uint8_t *payload, uint8_t payload_len);

typedef struct
{
    uint8_t func;
    infinite_protocol_handler_t handler;
} infinite_protocol_handler_entry_t;

uint8_t InfiniteProtocol_CalcChecksum(const uint8_t *buf, uint16_t len_without_checksum);

infinite_protocol_status_t InfiniteProtocol_UnpackAndDispatch(
    const uint8_t *rx_buf,
    uint16_t rx_len,
    const infinite_protocol_handler_entry_t *table,
    uint16_t table_size);

#ifdef __cplusplus
}
#endif

#endif /* INFINITE_PROTOCOL_H */
