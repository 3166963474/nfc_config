#ifndef __BUS_SLAVE_H__
#define __BUS_SLAVE_H__

#include "stdint.h"
#include "stdbool.h"
#include "string.h"
#include "main.h"
#include "pan_rf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUS_FRAME_HEAD_QUERY          0xA5
#define BUS_FRAME_HEAD_REPLY          0x5A

#define BUS_CMD_QUERY_STATUS          0x01
#define BUS_CMD_STATUS_REPLY          0x81

#define BUS_QUERY_FRAME_LEN           6
#define BUS_REPLY_FRAME_LEN           10

typedef enum
{
    BUS_SLAVE_IDLE = 0,
    BUS_SLAVE_WAIT_TX_DONE
} bus_slave_state_t;

typedef struct
{
    uint8_t vehicle_id;
    uint8_t seat_id;
    uint16_t full_id;

    bus_slave_state_t state;

    uint8_t last_seq;
    uint8_t sensor_cache[4];
		uint8_t senser_enable[4];

    uint32_t rx_ok_count;
    uint32_t rx_invalid_count;
    uint32_t tx_count;
} bus_slave_t;

void bus_slave_init(bus_slave_t *slave, uint8_t vehicle_id, uint8_t seat_id);
void bus_slave_start(bus_slave_t *slave);
void bus_slave_task(bus_slave_t *slave);

void bus_slave_on_rx_done(bus_slave_t *slave);
void bus_slave_on_tx_done(bus_slave_t *slave);
bus_slave_t *get_slave_obj(void);

#ifdef __cplusplus
}
#endif

#endif
