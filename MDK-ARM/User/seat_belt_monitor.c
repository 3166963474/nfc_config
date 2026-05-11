#include "seat_belt_monitor.h"

#include <string.h>
#include "main.h"
#include "buzzer_led_drv.h"

/*
 * GPIO mapping specified for the current hardware:
 *   seat0_belt = PB14
 *   seat0_seat = PB15
 *   seat1_belt = PB6
 *   seat1_seat = PB5
 */
#ifndef SEAT0_BELT_GPIO_PORT
#define SEAT0_BELT_GPIO_PORT GPIOB
#endif
#ifndef SEAT0_BELT_GPIO_PIN
#define SEAT0_BELT_GPIO_PIN  GPIO_PIN_14
#endif

#ifndef SEAT0_SEAT_GPIO_PORT
#define SEAT0_SEAT_GPIO_PORT GPIOB
#endif
#ifndef SEAT0_SEAT_GPIO_PIN
#define SEAT0_SEAT_GPIO_PIN  GPIO_PIN_15
#endif

#ifndef SEAT1_BELT_GPIO_PORT
#define SEAT1_BELT_GPIO_PORT GPIOB
#endif
#ifndef SEAT1_BELT_GPIO_PIN
#define SEAT1_BELT_GPIO_PIN  GPIO_PIN_6
#endif

#ifndef SEAT1_SEAT_GPIO_PORT
#define SEAT1_SEAT_GPIO_PORT GPIOB
#endif
#ifndef SEAT1_SEAT_GPIO_PIN
#define SEAT1_SEAT_GPIO_PIN  GPIO_PIN_5
#endif

/*
 * Default assumption:
 *   GPIO_PIN_SET means belt closed / seat occupied.
 * If the real hardware is active-low, override these macros in project settings
 * or before including this file.
 */
//#ifndef SEAT_BELT_MONITOR_BELT_CLOSED_LEVEL
//#define SEAT_BELT_MONITOR_BELT_CLOSED_LEVEL GPIO_PIN_RESET
//#endif

//#ifndef SEAT_BELT_MONITOR_SEAT_OCCUPIED_LEVEL
//#define SEAT_BELT_MONITOR_SEAT_OCCUPIED_LEVEL GPIO_PIN_RESET
//#endif
static GPIO_PinState SEAT_BELT_MONITOR_BELT_CLOSED_LEVEL = GPIO_PIN_SET;
static GPIO_PinState SEAT_BELT_MONITOR_SEAT_OCCUPIED_LEVEL = GPIO_PIN_RESET;

/* Set to 1 if you also want the red LED to follow the alarm state. */
#ifndef SEAT_BELT_MONITOR_USE_RED_LED
#define SEAT_BELT_MONITOR_USE_RED_LED 0U
#endif

#define ORDER_STATE_MIN             1U
#define ORDER_STATE_MAX             12U
#define ORDER_STATE_TO_MASK(order)  ((uint16_t)(1UL << ((uint8_t)(order) - 1U)))
#define UINT8_INVALID               0xFFU

seat_belt_monitor_seat_t s_seat[SEAT_PORT_COUNT];

/* Latest-only report mailbox. New reports overwrite old unsent reports. */
seat_belt_report_event_t s_report_mailbox;
static uint8_t s_report_pending = 0U;
static uint16_t s_event_seq = 0U;
static uint16_t s_event_drop_count = 0U;

static uint8_t s_alarm_output_active = 0U;

/* transition_map[old_state][new_state] -> order_state. 0 means no transition. */
static const uint8_t s_transition_map[4][4] =
{
    /* from 00 */ {0U,  1U,  8U, 11U},
    /* from 01 */ {2U,  0U, 10U,  3U},
    /* from 10 */ {7U,  9U,  0U,  6U},
    /* from 11 */ {12U, 4U,  5U,  0U}
};

static uint16_t seconds_to_ticks(uint8_t seconds)
{
    return (uint16_t)((uint16_t)seconds * (uint16_t)SEAT_BELT_MONITOR_TICK_HZ);
}

static uint8_t is_valid_seat_index(uint8_t seat_index)
{
    return (seat_index < SEAT_PORT_COUNT) ? 1U : 0U;
}

static uint8_t is_order_state_valid(uint8_t order_state)
{
    return ((order_state >= ORDER_STATE_MIN) && (order_state <= ORDER_STATE_MAX)) ? 1U : 0U;
}

static uint8_t order_mask_is_set(uint16_t mask, uint8_t order_state)
{
    if (is_order_state_valid(order_state) == 0U)
    {
        return 0U;
    }

    return ((mask & ORDER_STATE_TO_MASK(order_state)) != 0U) ? 1U : 0U;
}

static uint8_t read_gpio_as_1_if_match(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState active_level)
{
    return (HAL_GPIO_ReadPin(port, pin) == active_level) ? 1U : 0U;
}

static uint8_t read_raw_state(uint8_t seat_index)
{
    uint8_t belt = 0U;
    uint8_t seat = 0U;

    if (seat_index == 0U)
    {
        belt = read_gpio_as_1_if_match(SEAT0_BELT_GPIO_PORT,
                                       SEAT0_BELT_GPIO_PIN,
                                       SEAT_BELT_MONITOR_BELT_CLOSED_LEVEL);
        seat = read_gpio_as_1_if_match(SEAT0_SEAT_GPIO_PORT,
                                       SEAT0_SEAT_GPIO_PIN,
                                       SEAT_BELT_MONITOR_SEAT_OCCUPIED_LEVEL);
    }
    else
    {
        belt = read_gpio_as_1_if_match(SEAT1_BELT_GPIO_PORT,
                                       SEAT1_BELT_GPIO_PIN,
                                       SEAT_BELT_MONITOR_BELT_CLOSED_LEVEL);
        seat = read_gpio_as_1_if_match(SEAT1_SEAT_GPIO_PORT,
                                       SEAT1_SEAT_GPIO_PIN,
                                       SEAT_BELT_MONITOR_SEAT_OCCUPIED_LEVEL);
    }

    /* state = s1s0 = belt:seat. */
    return (uint8_t)((belt << 1U) | seat);
}

static uint8_t get_order_state(uint8_t old_state, uint8_t new_state)
{
    if ((old_state > 3U) || (new_state > 3U))
    {
        return SEAT_BELT_MONITOR_INVALID_ORDER_STATE;
    }

    return s_transition_map[old_state][new_state];
}

static void clear_candidate(seat_belt_monitor_seat_t *seat)
{
    seat->candidate_state = UINT8_INVALID;
    seat->candidate_order = SEAT_BELT_MONITOR_INVALID_ORDER_STATE;
    seat->candidate_ticks = 0U;
}

static void fill_report_snapshot(uint8_t changed_seat_index, uint8_t changed_order_state)
{
    uint8_t i;

    if (s_report_pending != 0U)
    {
        /* Old unsent data is intentionally discarded. */
        s_event_drop_count++;
    }

    s_event_seq++;
    if (s_event_seq == 0U)
    {
        s_event_seq = 1U;
    }

    (void)memset(&s_report_mailbox, 0, sizeof(s_report_mailbox));

    s_report_mailbox.seq = s_event_seq;
    s_report_mailbox.changed_seat_index = changed_seat_index;
    s_report_mailbox.changed_order_state = changed_order_state;
    s_report_mailbox.seat_count = SEAT_PORT_COUNT;

    for (i = 0U; i < SEAT_PORT_COUNT; i++)
    {
        s_report_mailbox.seat[i].enable = s_seat[i].enable;
        s_report_mailbox.seat[i].seat_no = s_seat[i].seat_no;

        if (s_seat[i].enable != 0U)
        {
            s_report_mailbox.seat[i].order_state = s_seat[i].current_order_state;
        }
        else
        {
            s_report_mailbox.seat[i].order_state = SEAT_BELT_MONITOR_INVALID_ORDER_STATE;
        }
    }

    s_report_pending = 1U;
}

static void alarm_start_active(seat_belt_monitor_seat_t *seat)
{
    seat->alarm_phase = SEAT_BELT_ALARM_PHASE_ACTIVE;

    if (slave_payload.seat_behavior.alarm_duration_s == 255U)
    {
        seat->alarm_duration_ticks = 0xFFFFU; /* until next confirmed state */
    }
    else
    {
        seat->alarm_duration_ticks = seconds_to_ticks(slave_payload.seat_behavior.alarm_duration_s);
        if (seat->alarm_duration_ticks == 0U)
        {
            seat->alarm_phase = SEAT_BELT_ALARM_PHASE_NONE;
        }
    }
}

static void alarm_reset_for_new_order(seat_belt_monitor_seat_t *seat, uint8_t order_state)
{
    uint8_t is_abnormal;

    seat->alarm_phase = SEAT_BELT_ALARM_PHASE_NONE;
    seat->alarm_delay_ticks = 0U;
    seat->alarm_duration_ticks = 0U;

    if (seat->enable == 0U)
    {
        return;
    }

    if (slave_payload.seat_behavior.alarm_duration_s == 0U)
    {
        return;
    }

    is_abnormal = order_mask_is_set(slave_payload.seat_behavior.abnormal_order_mask, order_state);
    if (is_abnormal == 0U)
    {
        return;
    }

    seat->alarm_delay_ticks = seconds_to_ticks(slave_payload.seat_behavior.alarm_delay_s);
    if (seat->alarm_delay_ticks > 0U)
    {
        seat->alarm_phase = SEAT_BELT_ALARM_PHASE_DELAY;
    }
    else
    {
        alarm_start_active(seat);
    }
}

static void confirm_transition(uint8_t seat_index, uint8_t new_state, uint8_t order_state)
{
    seat_belt_monitor_seat_t *seat;

    if ((is_valid_seat_index(seat_index) == 0U) || (is_order_state_valid(order_state) == 0U))
    {
        return;
    }

    seat = &s_seat[seat_index];
    if (seat->enable == 0U)
    {
        return;
    }

    seat->stable_state = new_state;
    seat->current_order_state = order_state;
    clear_candidate(seat);

    /* A confirmed transition on either seat generates one report containing both seats. */
    fill_report_snapshot(seat_index, order_state);

    alarm_reset_for_new_order(seat, order_state);
}

static void update_state_machine_for_seat(uint8_t seat_index)
{
    seat_belt_monitor_seat_t *seat;
    uint8_t raw_state;
    uint8_t order_state;
    uint8_t filter_enabled;
    uint16_t filter_ticks;

    if (is_valid_seat_index(seat_index) == 0U)
    {
        return;
    }

    seat = &s_seat[seat_index];

    if (seat->enable == 0U)
    {
        clear_candidate(seat);
        seat->current_order_state = SEAT_BELT_MONITOR_INVALID_ORDER_STATE;
        seat->alarm_phase = SEAT_BELT_ALARM_PHASE_NONE;
        seat->alarm_delay_ticks = 0U;
        seat->alarm_duration_ticks = 0U;
        return;
    }

    raw_state = read_raw_state(seat_index);

    if (raw_state == seat->stable_state)
    {
        clear_candidate(seat);
        return;
    }

    order_state = get_order_state(seat->stable_state, raw_state);
    if (is_order_state_valid(order_state) == 0U)
    {
        clear_candidate(seat);
        return;
    }

    filter_enabled = order_mask_is_set(slave_payload.seat_behavior.filter_order_mask, order_state);
    filter_ticks = seconds_to_ticks(slave_payload.seat_behavior.filter_time_s);

    if ((filter_enabled == 0U) || (filter_ticks == 0U))
    {
        confirm_transition(seat_index, raw_state, order_state);
        return;
    }

    if ((seat->candidate_state != raw_state) || (seat->candidate_order != order_state))
    {
        seat->candidate_state = raw_state;
        seat->candidate_order = order_state;
        seat->candidate_ticks = 1U;
    }
    else if (seat->candidate_ticks < 0xFFFFU)
    {
        seat->candidate_ticks++;
    }

    if (seat->candidate_ticks >= filter_ticks)
    {
        confirm_transition(seat_index, raw_state, order_state);
    }
}

static void update_alarm_timer_for_seat(uint8_t seat_index)
{
    seat_belt_monitor_seat_t *seat;

    if (is_valid_seat_index(seat_index) == 0U)
    {
        return;
    }

    seat = &s_seat[seat_index];
    if (seat->enable == 0U)
    {
        seat->alarm_phase = SEAT_BELT_ALARM_PHASE_NONE;
        return;
    }

    if (seat->alarm_phase == SEAT_BELT_ALARM_PHASE_DELAY)
    {
        if (seat->alarm_delay_ticks > 0U)
        {
            seat->alarm_delay_ticks--;
        }

        if (seat->alarm_delay_ticks == 0U)
        {
            alarm_start_active(seat);
        }
    }
    else if (seat->alarm_phase == SEAT_BELT_ALARM_PHASE_ACTIVE)
    {
        if (slave_payload.seat_behavior.alarm_duration_s != 255U)
        {
            if (seat->alarm_duration_ticks > 0U)
            {
                seat->alarm_duration_ticks--;
            }

            if (seat->alarm_duration_ticks == 0U)
            {
                seat->alarm_phase = SEAT_BELT_ALARM_PHASE_NONE;
            }
        }
    }
    else
    {
        /* Nothing to do. */
    }
}

static uint8_t any_seat_alarm_active(void)
{
    uint8_t i;

    for (i = 0U; i < SEAT_PORT_COUNT; i++)
    {
        if ((s_seat[i].enable != 0U) &&
            (s_seat[i].alarm_phase == SEAT_BELT_ALARM_PHASE_ACTIVE))
        {
            return 1U;
        }
    }

    return 0U;
}

static void update_alarm_output(void)
{
    uint8_t active = any_seat_alarm_active();

    if (active == s_alarm_output_active)
    {
        return;
    }

    s_alarm_output_active = active;

    if (active != 0U)
    {
        BuzzerLed_SetMode(BUZZER_LED_DEV_BUZZER, BUZZER_LED_MODE_FAST_BLINK);
#if (SEAT_BELT_MONITOR_USE_RED_LED != 0U)
        BuzzerLed_SetMode(BUZZER_LED_DEV_LED_R, BUZZER_LED_MODE_ON);
#endif
    }
    else
    {
        BuzzerLed_SetMode(BUZZER_LED_DEV_BUZZER, BUZZER_LED_MODE_OFF);
#if (SEAT_BELT_MONITOR_USE_RED_LED != 0U)
        BuzzerLed_SetMode(BUZZER_LED_DEV_LED_R, BUZZER_LED_MODE_OFF);
#endif
    }
}

void SeatBeltMonitor_Init(void)
{
    uint8_t i;

    (void)memset(s_seat, 0, sizeof(s_seat));
    SeatBeltMonitor_ClearReportEvents();

    s_event_seq = 0U;
    s_event_drop_count = 0U;
    s_alarm_output_active = 0U;

    for (i = 0U; i < SEAT_PORT_COUNT; i++)
    {
        s_seat[i].enable = ((uint8_t)(0x01&slave_payload.seat[i].enable) == SEAT_ENABLE_TRUE) ? 1U : 0U;
        s_seat[i].seat_no = slave_payload.seat[i].seat_no;
        s_seat[i].stable_state = read_raw_state(i);
        s_seat[i].current_order_state = SEAT_BELT_MONITOR_INVALID_ORDER_STATE;
        clear_candidate(&s_seat[i]);
    }
		if((slave_payload.reserved[0]&0x01) == 1)SEAT_BELT_MONITOR_SEAT_OCCUPIED_LEVEL = GPIO_PIN_SET;
		if((slave_payload.reserved[0]&0x02) == 1)SEAT_BELT_MONITOR_BELT_CLOSED_LEVEL = GPIO_PIN_RESET;
    BuzzerLed_SetMode(BUZZER_LED_DEV_BUZZER, BUZZER_LED_MODE_OFF);
#if (SEAT_BELT_MONITOR_USE_RED_LED != 0U)
    BuzzerLed_SetMode(BUZZER_LED_DEV_LED_R, BUZZER_LED_MODE_OFF);
#endif
}

void SeatBeltMonitor_Task10Hz(void)
{
    uint8_t i;

    for (i = 0U; i < SEAT_PORT_COUNT; i++)
    {
        update_state_machine_for_seat(i);
        update_alarm_timer_for_seat(i);
    }

    update_alarm_output();
}

uint8_t SeatBeltMonitor_HasReportEvent(void)
{
    return (s_report_pending != 0U) ? 1U : 0U;
}

uint8_t SeatBeltMonitor_PopReportEvent(seat_belt_report_event_t *event)
{
    if ((event == NULL) || (s_report_pending == 0U))
    {
        return 0U;
    }

    *event = s_report_mailbox;
    s_report_pending = 0U;

    return 1U;
}

void SeatBeltMonitor_ClearReportEvents(void)
{
    s_report_pending = 0U;
    (void)memset(&s_report_mailbox, 0, sizeof(s_report_mailbox));
}

uint16_t SeatBeltMonitor_GetDroppedEventCount(void)
{
    return s_event_drop_count;
}

uint8_t SeatBeltMonitor_GetStableState(uint8_t seat_index)
{
    if (is_valid_seat_index(seat_index) == 0U)
    {
        return UINT8_INVALID;
    }

    return s_seat[seat_index].stable_state;
}

uint8_t SeatBeltMonitor_GetCurrentOrderState(uint8_t seat_index)
{
    if (is_valid_seat_index(seat_index) == 0U)
    {
        return SEAT_BELT_MONITOR_INVALID_ORDER_STATE;
    }

    return s_seat[seat_index].current_order_state;
}

uint8_t SeatBeltMonitor_IsAlarmActive(void)
{
    return s_alarm_output_active;
}
