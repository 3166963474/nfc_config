#ifndef __BUS_SLAVE_REPORT_H__
#define __BUS_SLAVE_REPORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "pan_rf.h"
#include "app_st25dv.h"
#include "seat_belt_monitor.h"

/*
 * Uplink payload:
 * byte0   : vehicle_id
 * byte1~2 : seq, little-endian
 * byte3   : seat0 seat_no
 * byte4   : seat0 order_state
 * byte5   : seat1 seat_no
 * byte6   : seat1 order_state
 *
 * ACK payload:
 * byte0   : vehicle_id
 * byte1~2 : seq, little-endian
 */
#define BUS_SLAVE_REPORT_TX_FRAME_LEN   7U
#define BUS_SLAVE_REPORT_ACK_FRAME_LEN  3U

#define BUS_SLAVE_REPORT_DEFAULT_SLOT_MS          5U
#define BUS_SLAVE_REPORT_DEFAULT_CONTEND_N        50U
#define BUS_SLAVE_REPORT_DEFAULT_IDLE_CONFIRM_MS  10U
#define BUS_SLAVE_REPORT_DEFAULT_ACK_TIMEOUT_MS   35U
#define BUS_SLAVE_REPORT_DEFAULT_MAX_RETRY        255U

#define BUS_SLAVE_REPORT_TX_DONE_TIMEOUT_MS       500U

/* reserved[1] periodic-report base timing unit; reserved[2] is in seconds. */
#define BUS_SLAVE_REPORT_PERIODIC_UNIT_MS          8000UL
#define BUS_SLAVE_REPORT_RANDOM_DELAY_UNIT_MS      1000UL

/* 255 means retry forever. Other values are retry count after the first failed send. */
#define BUS_SLAVE_REPORT_MAX_RETRY_FOREVER        255U

typedef enum
{
    BUS_SLAVE_REPORT_IDLE = 0,
    BUS_SLAVE_REPORT_WAIT_CHANNEL_IDLE,
    BUS_SLAVE_REPORT_WAIT_CONTEND_SLOT,
    BUS_SLAVE_REPORT_SEND,
    BUS_SLAVE_REPORT_WAIT_TX_DONE,
    BUS_SLAVE_REPORT_WAIT_ACK
} bus_slave_report_state_t;

typedef struct
{
    uint8_t vehicle_id;

    uint8_t  slot_ms;
    uint8_t  contend_slot_count_n;
    uint8_t  idle_confirm_ms;
    uint16_t ack_timeout_ms;
    uint8_t  max_retry;

    /* reserved[1]: base interval in 8-second units; reserved[2]: random extra seconds. */
    uint8_t periodic_base_interval_units;
    uint8_t periodic_random_delay_units;

    bus_slave_report_state_t state;
    uint32_t state_tick;

    uint8_t  idle_confirm_active;
    uint32_t idle_start_tick;
    uint32_t contend_target_tick;
    uint32_t last_tx_start_tick;

    uint8_t retry_count;
    uint8_t current_valid;
    seat_belt_report_event_t current_event;

    uint8_t periodic_report_scheduled;
    uint32_t next_periodic_report_tick;

    uint32_t tx_count;
    uint32_t ack_ok_count;
    uint32_t ack_timeout_count;
    uint32_t ack_wrong_count;
    uint32_t tx_timeout_count;
    uint32_t retry_count_total;
    uint32_t drop_old_count;
    uint32_t pending_replace_count;
    uint32_t drop_after_retry_limit_count;
    uint32_t periodic_report_count;
} bus_slave_report_t;

void bus_slave_report_init(bus_slave_report_t *report);
void bus_slave_report_start(bus_slave_report_t *report);
void bus_slave_report_task(bus_slave_report_t *report, uint32_t now_ms);

void bus_slave_report_on_tx_done(bus_slave_report_t *report, uint32_t now_ms);
void bus_slave_report_on_rx_done(bus_slave_report_t *report, uint32_t now_ms);

const seat_belt_report_event_t *bus_slave_report_get_current_event(const bus_slave_report_t *report);
bus_slave_report_t *get_slave_report_obj(void);

/*
 * Channel idle hook.
 * Default implementation uses the existing CAD helper from pan_rf.c, because your old
 * bus_master code already uses check_cad_rx_inactive().
 *
 * If the final hardware exposes channel-busy on a GPIO, implement another non-weak
 * BusSlaveReport_IsChannelIdle() in your board file and read that GPIO there.
 * Return 1 when channel is idle, 0 when channel is busy.
 */
uint8_t BusSlaveReport_IsChannelIdle(void);

#ifdef __cplusplus
}
#endif

#endif /* __BUS_SLAVE_REPORT_H__ */
