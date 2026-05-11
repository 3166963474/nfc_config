#include "bus_slave_report.h"

#include <string.h>
#include "bus_rand.h"
#include "rs485bsp.h"

/* pan_rf.c里定义的接收结果结构体，沿用原bus_master/bus_slave代码风格。 */
extern struct RxDoneMsg RxDoneParams;

static bus_slave_report_t s_slave_report;

#define BSR_LOG(...) UART_DBG_Printf_DMA(__VA_ARGS__)

static const char *bus_slave_report_state_name(bus_slave_report_state_t state)
{
    switch (state)
    {
    case BUS_SLAVE_REPORT_IDLE:              return "IDLE";
    case BUS_SLAVE_REPORT_WAIT_CHANNEL_IDLE: return "WAIT_IDLE";
    case BUS_SLAVE_REPORT_WAIT_CONTEND_SLOT: return "WAIT_SLOT";
    case BUS_SLAVE_REPORT_SEND:              return "SEND";
    case BUS_SLAVE_REPORT_WAIT_TX_DONE:      return "WAIT_TX_DONE";
    case BUS_SLAVE_REPORT_WAIT_ACK:          return "WAIT_ACK";
    default:                                 return "UNKNOWN";
    }
}

static void bus_slave_report_enter_rx(void)
{
    rf_set_mode(RF_MODE_STB3);
    rf_enter_continous_rx();
}

__weak uint8_t BusSlaveReport_IsChannelIdle(void)
{
    uint32_t chirp_time = rf_get_chirp_time(DEFAULT_BW, DEFAULT_SF);
    return (check_cad_rx_inactive(chirp_time) == LEVEL_INACTIVE) ? 1U : 0U;
}

static uint8_t clamp_u8(uint8_t value, uint8_t min_value, uint8_t max_value, uint8_t fallback)
{
    if ((value < min_value) || (value > max_value))
    {
        return fallback;
    }
    return value;
}

static uint16_t clamp_u16(uint16_t value, uint16_t min_value, uint16_t max_value, uint16_t fallback)
{
    if ((value < min_value) || (value > max_value))
    {
        return fallback;
    }
    return value;
}

static void bus_slave_report_load_config(bus_slave_report_t *report)
{
    const slave_payload_t *cfg = &slave_payload;

    report->vehicle_id = cfg->vehicle_id;

    report->slot_ms = clamp_u8(cfg->contend.slot_ms,
                               SLAVE_SLOT_MS_MIN,
                               SLAVE_SLOT_MS_MAX,
                               BUS_SLAVE_REPORT_DEFAULT_SLOT_MS);

    report->contend_slot_count_n = clamp_u8(cfg->contend.contend_slot_count_n,
                                            SLAVE_CONTEND_N_MIN,
                                            SLAVE_CONTEND_N_MAX,
                                            BUS_SLAVE_REPORT_DEFAULT_CONTEND_N);

    report->idle_confirm_ms = clamp_u8(cfg->contend.idle_confirm_ms,
                                       SLAVE_IDLE_CONFIRM_MS_MIN,
                                       SLAVE_IDLE_CONFIRM_MS_MAX,
                                       BUS_SLAVE_REPORT_DEFAULT_IDLE_CONFIRM_MS);

    report->ack_timeout_ms = clamp_u16(cfg->contend.ack_timeout_ms,
                                       SLAVE_ACK_TIMEOUT_MS_MIN,
                                       SLAVE_ACK_TIMEOUT_MS_MAX,
                                       BUS_SLAVE_REPORT_DEFAULT_ACK_TIMEOUT_MS);

    report->max_retry = cfg->contend.max_retry;

    BSR_LOG("BSR cfg: vehicle=%u slot=%u N=%u idle=%u ack_to=%u max_retry=%u\r\n",
            report->vehicle_id,
            report->slot_ms,
            report->contend_slot_count_n,
            report->idle_confirm_ms,
            report->ack_timeout_ms,
            report->max_retry);
}

static void bus_slave_report_clear_timing(bus_slave_report_t *report)
{
    report->idle_confirm_active = 0U;
    report->idle_start_tick = 0U;
    report->contend_target_tick = 0U;
    report->last_tx_start_tick = 0U;
}

static void bus_slave_report_set_state(bus_slave_report_t *report,
                                       bus_slave_report_state_t state,
                                       uint32_t now_ms)
{
    if (report->state != state)
    {
        BSR_LOG("BSR state: %s -> %s \r\n",
                bus_slave_report_state_name(report->state),
                bus_slave_report_state_name(state));
    }

    report->state = state;
    report->state_tick = now_ms;
}

static void bus_slave_report_enter_idle(bus_slave_report_t *report, uint32_t now_ms)
{
    bus_slave_report_set_state(report, BUS_SLAVE_REPORT_IDLE, now_ms);
    bus_slave_report_clear_timing(report);
    bus_slave_report_enter_rx();
}

static void bus_slave_report_enter_wait_idle(bus_slave_report_t *report, uint32_t now_ms)
{
    bus_slave_report_set_state(report, BUS_SLAVE_REPORT_WAIT_CHANNEL_IDLE, now_ms);
    report->idle_confirm_active = 0U;
    report->idle_start_tick = 0U;
    report->contend_target_tick = 0U;
    bus_slave_report_enter_rx();
}

static uint8_t bus_slave_report_load_latest_event(bus_slave_report_t *report, const char *reason)
{
    seat_belt_report_event_t ev;

    if (SeatBeltMonitor_PopReportEvent(&ev) == 0U)
    {
        return 0U;
    }

    if (report->current_valid != 0U)
    {
        report->drop_old_count++;
        report->pending_replace_count++;
        BSR_LOG("BSR replace old: old_seq=%u new_seq=%u reason=%s\r\n",
                report->current_event.seq,
                ev.seq,
                (reason != NULL) ? reason : "unknown");
    }
    else
    {
        BSR_LOG("BSR load event: seq=%u reason=%s\r\n",
                ev.seq,
                (reason != NULL) ? reason : "unknown");
    }

    report->current_event = ev;
    report->current_valid = 1U;
    report->retry_count = 0U;
    return 1U;
}

static void bus_slave_report_start_contend(bus_slave_report_t *report, uint32_t now_ms)
{
    uint8_t slot_idx;

    slot_idx = bus_rand_slot(report->contend_slot_count_n);
    report->contend_target_tick = now_ms + ((uint32_t)slot_idx * (uint32_t)report->slot_ms);

    bus_slave_report_set_state(report, BUS_SLAVE_REPORT_WAIT_CONTEND_SLOT, now_ms);

    BSR_LOG("BSR contend: slot=%u target=%lu seq=%u\r\n",
            slot_idx,
            report->contend_target_tick,
            report->current_event.seq);
}

static void bus_slave_report_build_payload(const seat_belt_report_event_t *event,
                                           uint8_t frame[BUS_SLAVE_REPORT_TX_FRAME_LEN])
{
    uint8_t i;

    memset(frame, 0, BUS_SLAVE_REPORT_TX_FRAME_LEN);

    frame[0] = (uint8_t)(event->seq & 0xFFU);
    frame[1] = (uint8_t)((event->seq >> 8) & 0xFFU);

    for (i = 0U; i < SEAT_PORT_COUNT; i++)
    {
        uint8_t base = (uint8_t)(2U + (i * 2U));

        if (event->seat[i].enable != 0U)
        {
            frame[base] = event->seat[i].seat_no;
            frame[base + 1U] = event->seat[i].order_state;
        }
        else
        {
            frame[base] = 0U;
            frame[base + 1U] = 0U;
        }
    }
}

static void bus_slave_report_refresh_before_send(bus_slave_report_t *report)
{
    /*
     * 从开始等待连续空闲到真正发送之前，如果座椅状态机又产生了更新，
     * 只在发送前取一次最新快照并直接替换发送内容，不重新回到等待信道空闲。
     */
    (void)bus_slave_report_load_latest_event(report, "before_send");
}

static void bus_slave_report_send_current(bus_slave_report_t *report, uint32_t now_ms)
{
    uint8_t frame[BUS_SLAVE_REPORT_TX_FRAME_LEN];

    bus_slave_report_refresh_before_send(report);

    if (report->current_valid == 0U)
    {
        BSR_LOG("BSR send cancelled: no current event\r\n");
        bus_slave_report_enter_idle(report, now_ms);
        return;
    }

    bus_slave_report_build_payload(&report->current_event, frame);

    rf_set_mode(RF_MODE_STB3);
    rf_enter_continous_tx();
    rf_continous_tx_send_data(frame, BUS_SLAVE_REPORT_TX_FRAME_LEN);

    report->tx_count++;
    report->last_tx_start_tick = now_ms;
    bus_slave_report_set_state(report, BUS_SLAVE_REPORT_WAIT_TX_DONE, now_ms);

    BSR_LOG("BSR tx: seq=%u s0=%u/%u s1=%u/%u retry=%u\r\n",
            report->current_event.seq,
            frame[2], frame[3], frame[4], frame[5],
            report->retry_count);
}

static uint8_t bus_slave_report_parse_ack(const bus_slave_report_t *report,
                                          const uint8_t *buf,
                                          uint16_t len)
{
    uint16_t ack_seq;

    if ((buf == NULL) || (len != BUS_SLAVE_REPORT_ACK_FRAME_LEN))
    {
        return 0U;
    }

    ack_seq = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);

    return (ack_seq == report->current_event.seq) ? 1U : 0U;
}

static uint8_t bus_slave_report_can_retry(const bus_slave_report_t *report)
{
    if (report->max_retry == BUS_SLAVE_REPORT_MAX_RETRY_FOREVER)
    {
        return 1U;
    }

    return (report->retry_count < report->max_retry) ? 1U : 0U;
}

static void bus_slave_report_handle_send_failed(bus_slave_report_t *report, uint32_t now_ms)
{
    if (SeatBeltMonitor_HasReportEvent() != 0U)
    {
        (void)bus_slave_report_load_latest_event(report, "send_failed_new_event");
        bus_slave_report_enter_wait_idle(report, now_ms);
        return;
    }

    if (bus_slave_report_can_retry(report) != 0U)
    {
        report->retry_count++;
        report->retry_count_total++;
        BSR_LOG("BSR retry: seq=%u retry=%u\r\n",
                report->current_event.seq,
                report->retry_count);
        bus_slave_report_enter_wait_idle(report, now_ms);
        return;
    }

    BSR_LOG("BSR drop after retry limit: seq=%u retry=%u\r\n",
            report->current_event.seq,
            report->retry_count);

    report->drop_after_retry_limit_count++;
    report->current_valid = 0U;
    bus_slave_report_enter_idle(report, now_ms);
}

void bus_slave_report_init(bus_slave_report_t *report)
{
    if (report == NULL)
    {
        return;
    }

    memset(report, 0, sizeof(*report));
    bus_slave_report_load_config(report);
    report->state = BUS_SLAVE_REPORT_IDLE;
}

void bus_slave_report_start(bus_slave_report_t *report)
{
    if (report == NULL)
    {
        return;
    }

    report->current_valid = 0U;
    report->retry_count = 0U;
    bus_slave_report_enter_idle(report, 0U);
    BSR_LOG("BSR start\r\n");
}

void bus_slave_report_on_tx_done(bus_slave_report_t *report, uint32_t now_ms)
{
    if (report == NULL)
    {
        return;
    }

    rf_set_transmit_flag(RADIO_FLAG_IDLE);

    if (report->state == BUS_SLAVE_REPORT_WAIT_TX_DONE)
    {
        bus_slave_report_enter_rx();
        bus_slave_report_set_state(report, BUS_SLAVE_REPORT_WAIT_ACK, now_ms);
        BSR_LOG("BSR tx done: seq=%u duration=%lu\r\n",
                report->current_event.seq,
                now_ms - report->last_tx_start_tick);
    }
}

void bus_slave_report_on_rx_done(bus_slave_report_t *report, uint32_t now_ms)
{
    uint8_t ok;

    if (report == NULL)
    {
        return;
    }

    rf_set_recv_flag(RADIO_FLAG_IDLE);

    if (report->state != BUS_SLAVE_REPORT_WAIT_ACK)
    {
        BSR_LOG("BSR rx ignored: state=%s len=%u\r\n",
                bus_slave_report_state_name(report->state),
                RxDoneParams.Size);
        RxDoneParams.Size = 0U;
        return;
    }

    ok = bus_slave_report_parse_ack(report, RxDoneParams.Payload, RxDoneParams.Size);
    RxDoneParams.Size = 0U;

    if (ok != 0U)
    {
        BSR_LOG("BSR ack ok: seq=%u\r\n", report->current_event.seq);
        report->ack_ok_count++;
        report->current_valid = 0U;
        report->retry_count = 0U;
        bus_slave_report_enter_idle(report, now_ms);
    }
    else
    {
        BSR_LOG("BSR ack wrong: seq=%u\r\n", report->current_event.seq);
        report->ack_wrong_count++;
        bus_slave_report_handle_send_failed(report, now_ms);
    }
}

void bus_slave_report_task(bus_slave_report_t *report, uint32_t now_ms)
{
    if (report == NULL)
    {
        return;
    }

    if (rf_get_transmit_flag() == RADIO_FLAG_TXDONE)
    {
        bus_slave_report_on_tx_done(report, now_ms);
    }

    if (rf_get_recv_flag() == RADIO_FLAG_RXDONE)
    {
        bus_slave_report_on_rx_done(report, now_ms);
    }
    else if (rf_get_recv_flag() == RADIO_FLAG_RXERR)
    {
        BSR_LOG("BSR rx err\r\n");
        rf_set_recv_flag(RADIO_FLAG_IDLE);
        bus_slave_report_enter_rx();
    }
    else if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT)
    {
        BSR_LOG("BSR rx timeout flag\r\n");
        rf_set_recv_flag(RADIO_FLAG_IDLE);
        bus_slave_report_enter_rx();
    }

    switch (report->state)
    {
    case BUS_SLAVE_REPORT_IDLE:
        (void)bus_slave_report_load_latest_event(report, "idle");

        if (report->current_valid != 0U)
        {
            bus_slave_report_enter_wait_idle(report, now_ms);
        }
        break;

    case BUS_SLAVE_REPORT_WAIT_CHANNEL_IDLE:
        /*
         * 等待连续空闲期间不再取新事件。
         * 如果此阶段产生了新事件，会保留在SeatBeltMonitor的mailbox中，
         * 到真正发送前再替换当前发送内容。
         */
        if (report->current_valid == 0U)
        {
            bus_slave_report_enter_idle(report, now_ms);
            break;
        }

        if (BusSlaveReport_IsChannelIdle() != 0U)
        {
            if (report->idle_confirm_active == 0U)
            {
                report->idle_confirm_active = 1U;
                report->idle_start_tick = now_ms;
                BSR_LOG("BSR idle confirm start: seq=%u \r\n",
                        report->current_event.seq);
            }
            else if ((uint32_t)(now_ms - report->idle_start_tick) >= report->idle_confirm_ms)
            {
                report->idle_confirm_active = 0U;
                BSR_LOG("BSR idle confirmed: seq=%u elapsed=%lu\r\n",
                        report->current_event.seq,
                        now_ms - report->idle_start_tick);
                bus_slave_report_start_contend(report, now_ms);
            }
        }
        else
        {
            if (report->idle_confirm_active != 0U)
            {
                BSR_LOG("BSR idle confirm broken: seq=%u elapsed=%lu\r\n",
                        report->current_event.seq,
                        now_ms - report->idle_start_tick);
            }
            report->idle_confirm_active = 0U;
            report->idle_start_tick = 0U;
        }
        break;

    case BUS_SLAVE_REPORT_WAIT_CONTEND_SLOT:
        /*
         * 退避期间如果产生新事件，不返回等待空闲；只在发送前替换内容。
         * 退避期间如果信道忙，仍然放弃本次slot并回到等待连续空闲。
         */
        if (BusSlaveReport_IsChannelIdle() == 0U)
        {
            BSR_LOG("BSR contend busy: seq=%u at %lu\r\n",
                    report->current_event.seq,
                    now_ms);
            bus_slave_report_enter_wait_idle(report, now_ms);
            break;
        }

        if ((int32_t)(now_ms - report->contend_target_tick) >= 0)
        {
            bus_slave_report_set_state(report, BUS_SLAVE_REPORT_SEND, now_ms);
        }
        break;

    case BUS_SLAVE_REPORT_SEND:
        if (report->current_valid != 0U)
        {
            bus_slave_report_send_current(report, now_ms);
        }
        else
        {
            bus_slave_report_enter_idle(report, now_ms);
        }
        break;

    case BUS_SLAVE_REPORT_WAIT_TX_DONE:
        if ((uint32_t)(now_ms - report->state_tick) > BUS_SLAVE_REPORT_TX_DONE_TIMEOUT_MS)
        {
            BSR_LOG("BSR tx done timeout: seq=%u elapsed=%lu\r\n",
                    report->current_event.seq,
                    now_ms - report->state_tick);
            rf_set_transmit_flag(RADIO_FLAG_IDLE);
            report->tx_timeout_count++;
            bus_slave_report_handle_send_failed(report, now_ms);
        }
        break;

    case BUS_SLAVE_REPORT_WAIT_ACK:
        if ((uint32_t)(now_ms - report->state_tick) >= report->ack_timeout_ms)
        {
            BSR_LOG("BSR ack timeout: seq=%u elapsed=%lu\r\n",
                    report->current_event.seq,
                    now_ms - report->state_tick);
            report->ack_timeout_count++;
            bus_slave_report_handle_send_failed(report, now_ms);
        }
        break;

    default:
        BSR_LOG("BSR default reset: state=%d\r\n", report->state);
        report->current_valid = 0U;
        bus_slave_report_enter_idle(report, now_ms);
        break;
    }
}

const seat_belt_report_event_t *bus_slave_report_get_current_event(const bus_slave_report_t *report)
{
    if ((report == NULL) || (report->current_valid == 0U))
    {
        return NULL;
    }

    return &report->current_event;
}

bus_slave_report_t *get_slave_report_obj(void)
{
    return &s_slave_report;
}
