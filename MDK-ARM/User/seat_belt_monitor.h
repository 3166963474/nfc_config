#ifndef SEAT_BELT_MONITOR_H
#define SEAT_BELT_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "app_st25dv.h"

#define SEAT_BELT_MONITOR_TICK_HZ              10U
#define SEAT_BELT_MONITOR_INVALID_ORDER_STATE  0U

/* Seat combined state: bit1 = belt/s1, bit0 = seat/s0. */
typedef enum
{
    SEAT_BELT_STATE_00_EMPTY_UNBUCKLED    = 0U,
    SEAT_BELT_STATE_01_OCCUPIED_UNBUCKLED = 1U,
    SEAT_BELT_STATE_10_EMPTY_BUCKLED      = 2U,
    SEAT_BELT_STATE_11_OCCUPIED_BUCKLED   = 3U
} seat_belt_combined_state_t;

typedef enum
{
    SEAT_BELT_ALARM_PHASE_NONE = 0U,
    SEAT_BELT_ALARM_PHASE_DELAY,
    SEAT_BELT_ALARM_PHASE_ACTIVE
} seat_belt_alarm_phase_t;

typedef struct
{
    uint8_t enable;       /* 1: this seat is enabled and should be reported. */
    uint8_t seat_no;      /* NFC configured seat number. */
    uint8_t order_state;  /* Latest confirmed order_state, 1~12. 0 means no confirmed transition yet / invalid. */
} seat_belt_report_seat_t;

typedef struct
{
    uint16_t seq;                 /* Local report sequence, useful for LoRa ACK matching. */
    uint8_t  changed_seat_index;  /* Which seat generated this report request. 0 or 1. */
    uint8_t  changed_order_state; /* The newly confirmed order_state that triggered this report. */
    uint8_t  seat_count;          /* Always SEAT_PORT_COUNT in this design. */
    seat_belt_report_seat_t seat[SEAT_PORT_COUNT]; /* Snapshot of both seats. */
} seat_belt_report_event_t;

typedef struct
{
    uint8_t enable;
    uint8_t seat_no;

    uint8_t stable_state;       /* Current confirmed s1s0 state. */
    uint8_t candidate_state;    /* Pending raw state while filter is running. */
    uint8_t candidate_order;    /* Pending order_state while filter is running. */
    uint16_t candidate_ticks;   /* How long candidate_state has lasted. */

    uint8_t current_order_state;

    seat_belt_alarm_phase_t alarm_phase;
    uint16_t alarm_delay_ticks;
    uint16_t alarm_duration_ticks;
} seat_belt_monitor_seat_t;

void SeatBeltMonitor_Init(void);
void SeatBeltMonitor_Task10Hz(void);

/*
 * Latest-only report mailbox:
 * - A confirmed transition on either seat overwrites the previous unsent report.
 * - Each report contains a snapshot of both seat slots.
 * - Pop clears the pending flag; the sending state machine should keep its own copy until ACK.
 */
uint8_t SeatBeltMonitor_HasReportEvent(void);
uint8_t SeatBeltMonitor_PopReportEvent(seat_belt_report_event_t *event);
void SeatBeltMonitor_ClearReportEvents(void);
uint16_t SeatBeltMonitor_GetDroppedEventCount(void);

uint8_t SeatBeltMonitor_GetStableState(uint8_t seat_index);
uint8_t SeatBeltMonitor_GetCurrentOrderState(uint8_t seat_index);
uint8_t SeatBeltMonitor_IsAlarmActive(void);

#ifdef __cplusplus
}
#endif

#endif /* SEAT_BELT_MONITOR_H */
