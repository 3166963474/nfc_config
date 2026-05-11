#ifndef __BUS_MASTER_REPORT_H__
#define __BUS_MASTER_REPORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "pan_rf.h"
#include "app_st25dv.h"

/*
 * Master for the new slave-active-report mechanism.
 *
 * Slave uplink payload, no head/checksum:
 *   byte0~1 : seq, little-endian
 *   byte2   : seat0 seat_no, 0 if disabled
 *   byte3   : seat0 order_state, 0 if disabled / no confirmed transition
 *   byte4   : seat1 seat_no, 0 if disabled
 *   byte5   : seat1 order_state, 0 if disabled / no confirmed transition
 *
 * Master ACK payload, no head/checksum:
 *   byte0~1 : seq, little-endian
 */
#define BUS_MASTER_REPORT_RX_FRAME_LEN      6U
#define BUS_MASTER_REPORT_ACK_FRAME_LEN     2U
#define BUS_MASTER_REPORT_TX_DONE_TIMEOUT_MS 500U
#define BUS_MASTER_REPORT_DUP_WINDOW_MS     30000UL

#define BUS_MASTER_REPORT_ORDER_STATE_MAX   12U

typedef enum
{
    BUS_MASTER_REPORT_IDLE = 0,
    BUS_MASTER_REPORT_WAIT_ACK_TX_DONE
} bus_master_report_state_t;

typedef struct
{
    uint16_t seq;
    uint8_t seat0_no;
    uint8_t seat0_order_state;
    uint8_t seat1_no;
    uint8_t seat1_order_state;
} bus_master_slave_report_t;

typedef struct
{
    uint8_t vehicle_id;

    bus_master_report_state_t state;
    uint32_t state_tick;
    uint32_t last_ack_tx_start_tick;

    bus_master_slave_report_t current_report;
    bus_master_slave_report_t last_report;
    uint8_t last_report_valid;
    uint32_t last_report_tick;

    uint32_t rx_ok_count;
    uint32_t rx_invalid_count;
    uint32_t ack_tx_count;
    uint32_t ack_tx_done_count;
    uint32_t ack_tx_timeout_count;
    uint32_t rs485_report_count;
    uint32_t rs485_report_fail_count;
    uint32_t duplicate_count;
    uint32_t rx_err_count;
    uint32_t rx_timeout_count;
} bus_master_report_t;

void bus_master_report_init(bus_master_report_t *master);
void bus_master_report_start(bus_master_report_t *master);
void bus_master_report_task(bus_master_report_t *master, uint32_t now_ms);

void bus_master_report_on_rx_done(bus_master_report_t *master, uint32_t now_ms);
void bus_master_report_on_tx_done(bus_master_report_t *master, uint32_t now_ms);

const bus_master_slave_report_t *bus_master_report_get_last_report(const bus_master_report_t *master);
bus_master_report_t *get_master_report_obj(void);

#ifdef __cplusplus
}
#endif

#endif /* __BUS_MASTER_REPORT_H__ */
