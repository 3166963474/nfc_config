#include "seat_belt_monitor.h"

#include <stdint.h>
#include <string.h>

#include "main.h"
#include "buzzer_led_drv.h"
#include "bus_slave.h"
#include "app_st25dv.h"
/*
 * 说明：
 * 1. 本模块仅在从机模式下工作：BUS_MASTER_DEF == 0
 * 2. 每个从机有两个座位：
 *      seat0_belt = sensor[2]
 *      seat0_seat = sensor[3]
 *      seat1_belt = sensor[1]
 *      seat1_seat = sensor[0]
 * 3. reserved[1]：蜂鸣器响铃时长（秒）
 *      0x00 : 不响
 *      0x01~0xFE : 响对应秒数
 *      0xFF : 一直响
 * 4. reserved[2]：延时多久开始响（秒）
 * 5. 第一次响完后，如果人还在座位上，则不再重新计时；
 *    必须等人离座后再次坐下，才重新开始计时。
 */

/* ===== 配置 ===== */
#define SEAT_BELT_MONITOR_TICK_HZ      10u
#define SEAT_BELT_MONITOR_SEAT_NUM     2u
#define SEAT_BELT_MONITOR_FOREVER_TICK 0xFFFFFFFFu

typedef struct
{
    uint32_t unbelt_ticks;
    uint32_t buzzer_ticks_left;
    uint8_t alarm_active;
    uint8_t alarm_done_for_this_occupancy;
} seat_belt_alarm_state_t;

typedef struct
{
    seat_belt_alarm_state_t seat[SEAT_BELT_MONITOR_SEAT_NUM];
} seat_belt_monitor_ctx_t;

static seat_belt_monitor_ctx_t g_seat_belt_monitor_ctx;

/* ===== 内部函数 ===== */

static uint8_t SeatBeltMonitor_GetSeatOccupied(const uint8_t sensor[4], uint8_t seat_index)
{
    if (seat_index == 0u)
    {
        return (sensor[3] != 0u) ? 1u : 0u;
    }
    else
    {
        return (sensor[0] != 0u) ? 1u : 0u;
    }
}

static uint8_t SeatBeltMonitor_GetSeatBelted(const uint8_t sensor[4], uint8_t seat_index)
{
    if (seat_index == 0u)
    {
        return (sensor[2] != 0u) ? 1u : 0u;
    }
    else
    {
        return (sensor[1] != 0u) ? 1u : 0u;
    }
}

static uint8_t SeatBeltMonitor_IsUnbeltOccupied(const uint8_t sensor[4], uint8_t seat_index)
{
    uint8_t occupied;
    uint8_t belted;

    occupied = SeatBeltMonitor_GetSeatOccupied(sensor, seat_index);
    belted = SeatBeltMonitor_GetSeatBelted(sensor, seat_index);

    return ((occupied != 0u) && (belted == 0u)) ? 1u : 0u;
}

static uint8_t SeatBeltMonitor_GetSeatOccupiedOnly(const uint8_t sensor[4], uint8_t seat_index)
{
    return SeatBeltMonitor_GetSeatOccupied(sensor, seat_index);
}

static void SeatBeltMonitor_GetConfig(uint8_t *buzz_sec, uint8_t *delay_sec)
{
    slave_payload_t *payload;

    if ((buzz_sec == 0) || (delay_sec == 0))
    {
        return;
    }

    payload = (slave_payload_t *)payload_GetObject();
    if (payload == 0)
    {
        *buzz_sec = 0u;
        *delay_sec = 0u;
        return;
    }

    *buzz_sec = payload->reserved[1];
    *delay_sec = payload->reserved[2];
}

static void SeatBeltMonitor_StopOne(seat_belt_alarm_state_t *state)
{
    if (state == 0)
    {
        return;
    }

    state->unbelt_ticks = 0u;
    state->buzzer_ticks_left = 0u;
    state->alarm_active = 0u;
    state->alarm_done_for_this_occupancy = 0u;
}

static void SeatBeltMonitor_ResetAll(void)
{
    uint8_t i;

    for (i = 0u; i < SEAT_BELT_MONITOR_SEAT_NUM; i++)
    {
        SeatBeltMonitor_StopOne(&g_seat_belt_monitor_ctx.seat[i]);
    }

    BuzzerLed_SetMode(BUZZER_LED_DEV_BUZZER, BUZZER_LED_MODE_OFF);
}

static void SeatBeltMonitor_StartAlarm(seat_belt_alarm_state_t *state, uint8_t buzz_sec)
{
    if (state == 0)
    {
        return;
    }

    if (buzz_sec == 0u)
    {
        state->alarm_active = 0u;
        state->buzzer_ticks_left = 0u;
        state->alarm_done_for_this_occupancy = 1u;
        return;
    }

    state->alarm_active = 1u;
    state->alarm_done_for_this_occupancy = 1u;

    if (buzz_sec == 0xFFu)
    {
        state->buzzer_ticks_left = SEAT_BELT_MONITOR_FOREVER_TICK;
    }
    else
    {
        state->buzzer_ticks_left = (uint32_t)buzz_sec * SEAT_BELT_MONITOR_TICK_HZ;
    }
}

/* ===== 对外接口 ===== */

void SeatBeltMonitor_Init(void)
{
    memset(&g_seat_belt_monitor_ctx, 0, sizeof(g_seat_belt_monitor_ctx));
}

void SeatBeltMonitor_Tick10Hz(void)
{
    uint8_t sensor[4];
    uint8_t buzz_sec;
    uint8_t delay_sec;
    uint32_t delay_ticks;
    uint8_t seat_index;
    uint8_t any_alarm_active = 0u;

    if (BUS_MASTER_DEF != 0u)
    {
        //SeatBeltMonitor_ResetAll();
        return;
    }
		bus_slave_t *slave_obj;
		slave_obj = get_slave_obj();
    memcpy(sensor, slave_obj->sensor_cache, sizeof(sensor));
		
    SeatBeltMonitor_GetConfig(&buzz_sec, &delay_sec);
    delay_ticks = (uint32_t)delay_sec * SEAT_BELT_MONITOR_TICK_HZ;

    for (seat_index = 0u; seat_index < SEAT_BELT_MONITOR_SEAT_NUM; seat_index++)
    {
        seat_belt_alarm_state_t *state = &g_seat_belt_monitor_ctx.seat[seat_index];
        uint8_t occupied;
        uint8_t unbelt_occupied;

        occupied = SeatBeltMonitor_GetSeatOccupiedOnly(sensor, seat_index);
        unbelt_occupied = SeatBeltMonitor_IsUnbeltOccupied(sensor, seat_index);

        if (occupied == 0u)
        {
            /* 人离座，整次占座状态清零 */
            SeatBeltMonitor_StopOne(state);
        }
        else
        {
            if (unbelt_occupied != 0u)
            {
                if ((state->alarm_active == 0u) &&
                    (state->alarm_done_for_this_occupancy == 0u))
                {
                    if (state->unbelt_ticks < delay_ticks)
                    {
                        state->unbelt_ticks++;
                    }

                    if (state->unbelt_ticks >= delay_ticks)
                    {
                        SeatBeltMonitor_StartAlarm(state, buzz_sec);
                    }
                }
                else if (state->alarm_active != 0u)
                {
                    if (state->buzzer_ticks_left == SEAT_BELT_MONITOR_FOREVER_TICK)
                    {
                        /* 一直响 */
                    }
                    else if (state->buzzer_ticks_left > 0u)
                    {
                        state->buzzer_ticks_left--;

                        if (state->buzzer_ticks_left == 0u)
                        {
                            /* 响完后保持 done 状态，但不再重新计时 */
                            state->alarm_active = 0u;
                        }
                    }
                    else
                    {
                        state->alarm_active = 0u;
                    }
                }
                else
                {
                    /* 已经响过一次且人没离座，不再重复计时 */
                }
            }
            else
            {
                /* 人还在，但已系安全带：
                   停止当前报警，但保留本次占座已处理状态，不再重复提醒 */
                state->unbelt_ticks = 0u;
                state->buzzer_ticks_left = 0u;
                state->alarm_active = 0u;
            }
        }

        if (state->alarm_active != 0u)
        {
            if ((state->buzzer_ticks_left > 0u) ||
                (state->buzzer_ticks_left == SEAT_BELT_MONITOR_FOREVER_TICK))
            {
                any_alarm_active = 1u;
            }
        }
    }

    if (any_alarm_active != 0u)
    {
			if(BuzzerLed_GetMode(BUZZER_LED_DEV_BUZZER) != BUZZER_LED_MODE_SLOW_BLINK)
        BuzzerLed_SetMode(BUZZER_LED_DEV_BUZZER, BUZZER_LED_MODE_SLOW_BLINK);
    }
    else
    {
			if(BuzzerLed_GetMode(BUZZER_LED_DEV_BUZZER) != BUZZER_LED_MODE_OFF)
        BuzzerLed_SetMode(BUZZER_LED_DEV_BUZZER, BUZZER_LED_MODE_OFF);
    }
}
