#include "bus_master_report.h"

#include <string.h>
#include "rs485bsp.h"

/* pan_rf.c里定义的接收结果结构体，沿用原bus_master/bus_slave代码风格。 */
extern struct RxDoneMsg RxDoneParams;

static bus_master_report_t s_master_report;

#define BMR_LOG(...) UART_DBG_Printf_DMA(__VA_ARGS__)

static const char *bus_master_report_state_name(bus_master_report_state_t state)
{
    switch (state)
    {
    case BUS_MASTER_REPORT_IDLE:             return "IDLE";
    case BUS_MASTER_REPORT_WAIT_ACK_TX_DONE: return "WAIT_ACK_TX_DONE";
    default:                                 return "UNKNOWN";
    }
}

static void bus_master_report_enter_rx(void)
{
    rf_set_mode(RF_MODE_STB3);
    rf_enter_continous_rx();
}

static void bus_master_report_set_state(bus_master_report_t *master,
                                        bus_master_report_state_t state,
                                        uint32_t now_ms)
{
    if (master->state != state)
    {
        BMR_LOG("BMR state: %s -> %s at %lu\r\n",
                bus_master_report_state_name(master->state),
                bus_master_report_state_name(state),
                now_ms);
    }

    master->state = state;
    master->state_tick = now_ms;
}

static uint8_t bus_master_report_order_state_valid(uint8_t seat_no, uint8_t order_state)
{
    if (seat_no == 0U)
    {
        return (order_state == 0U) ? 1U : 0U;
    }

    return (order_state <= BUS_MASTER_REPORT_ORDER_STATE_MAX) ? 1U : 0U;
}

static uint8_t bus_master_report_parse(const uint8_t *buf,
                                       uint16_t len,
                                       bus_master_slave_report_t *out)
{
    uint8_t has_seat;

    if ((buf == NULL) || (out == NULL))
    {
        return 0U;
    }

    if (len != BUS_MASTER_REPORT_RX_FRAME_LEN)
    {
        return 0U;
    }

    out->seq = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    out->seat0_no = buf[2];
    out->seat0_order_state = buf[3];
    out->seat1_no = buf[4];
    out->seat1_order_state = buf[5];

    if (bus_master_report_order_state_valid(out->seat0_no, out->seat0_order_state) == 0U)
    {
        return 0U;
    }

    if (bus_master_report_order_state_valid(out->seat1_no, out->seat1_order_state) == 0U)
    {
        return 0U;
    }

    has_seat = ((out->seat0_no != 0U) || (out->seat1_no != 0U)) ? 1U : 0U;
    if (has_seat == 0U)
    {
        return 0U;
    }

    return 1U;
}

static uint8_t bus_master_report_is_same(const bus_master_slave_report_t *a,
                                         const bus_master_slave_report_t *b)
{
    if ((a == NULL) || (b == NULL))
    {
        return 0U;
    }

    return ((a->seq == b->seq) &&
            (a->seat0_no == b->seat0_no) &&
            (a->seat0_order_state == b->seat0_order_state) &&
            (a->seat1_no == b->seat1_no) &&
            (a->seat1_order_state == b->seat1_order_state)) ? 1U : 0U;
}

static uint8_t bus_master_report_is_duplicate(const bus_master_report_t *master,
                                              const bus_master_slave_report_t *report,
                                              uint32_t now_ms)
{
    if ((master == NULL) || (report == NULL))
    {
        return 0U;
    }

    if (master->last_report_valid == 0U)
    {
        return 0U;
    }

    if (bus_master_report_is_same(&master->last_report, report) == 0U)
    {
        return 0U;
    }

    if ((uint32_t)(now_ms - master->last_report_tick) > BUS_MASTER_REPORT_DUP_WINDOW_MS)
    {
        return 0U;
    }

    return 1U;
}

static void bus_master_report_send_ack(bus_master_report_t *master, uint16_t seq, uint32_t now_ms)
{
    uint8_t ack[BUS_MASTER_REPORT_ACK_FRAME_LEN];

    ack[0] = (uint8_t)(seq & 0xFFU);
    ack[1] = (uint8_t)((seq >> 8) & 0xFFU);

    rf_set_mode(RF_MODE_STB3);
    rf_enter_continous_tx();
    rf_continous_tx_send_data(ack, BUS_MASTER_REPORT_ACK_FRAME_LEN);

    master->ack_tx_count++;
    master->last_ack_tx_start_tick = now_ms;
    bus_master_report_set_state(master, BUS_MASTER_REPORT_WAIT_ACK_TX_DONE, now_ms);

    BMR_LOG("BMR ack tx: seq=%u\r\n", seq);
}

static void bus_master_report_forward_to_rs485(bus_master_report_t *master,
                                               const bus_master_slave_report_t *report)
{
    HAL_StatusTypeDef st;

    if ((master == NULL) || (report == NULL))
    {
        return;
    }

    st = RS485BSP_SendTwoSeatReport(report->seat0_no,
                                    report->seat0_order_state,
                                    report->seat1_no,
                                    report->seat1_order_state);
    if (st == HAL_OK)
    {
        master->rs485_report_count++;
        BMR_LOG("BMR rs485 report: seq=%u s0=%u/%u s1=%u/%u\r\n",
                report->seq,
                report->seat0_no,
                report->seat0_order_state,
                report->seat1_no,
                report->seat1_order_state);
    }
    else
    {
        master->rs485_report_fail_count++;
        BMR_LOG("BMR rs485 report failed: seq=%u st=%d\r\n",
                report->seq,
                (int)st);
    }
}

void bus_master_report_init(bus_master_report_t *master)
{
    if (master == NULL)
    {
        return;
    }

    (void)memset(master, 0, sizeof(*master));
    master->vehicle_id = master_payload.vehicle_id;
    master->state = BUS_MASTER_REPORT_IDLE;

    BMR_LOG("BMR cfg: vehicle=%u\r\n", master->vehicle_id);
}

void bus_master_report_start(bus_master_report_t *master)
{
    if (master == NULL)
    {
        return;
    }

    bus_master_report_set_state(master, BUS_MASTER_REPORT_IDLE, 0U);
    bus_master_report_enter_rx();
    BMR_LOG("BMR start\r\n");
}

void bus_master_report_on_rx_done(bus_master_report_t *master, uint32_t now_ms)
{
    bus_master_slave_report_t report;
    uint8_t ok;
    uint8_t duplicate;

    if (master == NULL)
    {
        return;
    }
    rf_set_recv_flag(RADIO_FLAG_IDLE);
    ok = bus_master_report_parse(RxDoneParams.Payload, RxDoneParams.Size, &report);
    RxDoneParams.Size = 0U;

    if (ok == 0U)
    {
        master->rx_invalid_count++;
        BMR_LOG("BMR rx invalid\r\n");
        bus_master_report_enter_rx();
        return;
    }

    master->rx_ok_count++;
    master->current_report = report;
		uint8_t awdawf[16];
		float_to_str_2(awdawf,16,RxDoneParams.Snr);
    BMR_LOG("BMR rx ok: seq=%u s0=%u/%u s1=%u/%u rssi=%d snr= %s\r\n",
            report.seq,
            report.seat0_no,
            report.seat0_order_state,
            report.seat1_no,
            report.seat1_order_state,
            RxDoneParams.Rssi,
            awdawf);

    duplicate = bus_master_report_is_duplicate(master, &report, now_ms);
    if (duplicate != 0U)
    {
        master->duplicate_count++;
        BMR_LOG("BMR duplicate: seq=%u, ack only\r\n", report.seq);
    }
    else
    {
        bus_master_report_forward_to_rs485(master, &report);

        master->last_report = report;
        master->last_report_valid = 1U;
        master->last_report_tick = now_ms;
    }

    /* ACK must be sent even for duplicate reports, otherwise the slave will keep retrying. */
    bus_master_report_send_ack(master, report.seq, now_ms);
}

void bus_master_report_on_tx_done(bus_master_report_t *master, uint32_t now_ms)
{
    if (master == NULL)
    {
        return;
    }

    rf_set_transmit_flag(RADIO_FLAG_IDLE);

    if (master->state == BUS_MASTER_REPORT_WAIT_ACK_TX_DONE)
    {
        master->ack_tx_done_count++;
        BMR_LOG("BMR ack tx done: seq=%u duration=%lu\r\n",
                master->current_report.seq,
                now_ms - master->last_ack_tx_start_tick);
        bus_master_report_set_state(master, BUS_MASTER_REPORT_IDLE, now_ms);
        bus_master_report_enter_rx();
    }
    else
    {
        BMR_LOG("BMR tx done ignored: state=%s\r\n",
                bus_master_report_state_name(master->state));
        bus_master_report_enter_rx();
    }
}

void bus_master_report_task(bus_master_report_t *master, uint32_t now_ms)
{
    if (master == NULL)
    {
        return;
    }

    if (rf_get_transmit_flag() == RADIO_FLAG_TXDONE)
    {
        bus_master_report_on_tx_done(master, now_ms);
    }

    if (rf_get_recv_flag() == RADIO_FLAG_RXDONE)
    {
        bus_master_report_on_rx_done(master, now_ms);
    }
    else if (rf_get_recv_flag() == RADIO_FLAG_RXERR)
    {
        master->rx_err_count++;
        rf_set_recv_flag(RADIO_FLAG_IDLE);
        BMR_LOG("BMR rx err\r\n");
        bus_master_report_enter_rx();
    }
    else if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT)
    {
        master->rx_timeout_count++;
        rf_set_recv_flag(RADIO_FLAG_IDLE);
        BMR_LOG("BMR rx timeout\r\n");
        bus_master_report_enter_rx();
    }

    switch (master->state)
    {
    case BUS_MASTER_REPORT_IDLE:
        break;

    case BUS_MASTER_REPORT_WAIT_ACK_TX_DONE:
        if ((uint32_t)(now_ms - master->state_tick) > BUS_MASTER_REPORT_TX_DONE_TIMEOUT_MS)
        {
            master->ack_tx_timeout_count++;
            rf_set_transmit_flag(RADIO_FLAG_IDLE);
            BMR_LOG("BMR ack tx timeout: seq=%u elapsed=%lu\r\n",
                    master->current_report.seq,
                    now_ms - master->state_tick);
            bus_master_report_set_state(master, BUS_MASTER_REPORT_IDLE, now_ms);
            bus_master_report_enter_rx();
        }
        break;

    default:
        BMR_LOG("BMR default reset: state=%d\r\n", master->state);
        bus_master_report_set_state(master, BUS_MASTER_REPORT_IDLE, now_ms);
        bus_master_report_enter_rx();
        break;
    }
}

const bus_master_slave_report_t *bus_master_report_get_last_report(const bus_master_report_t *master)
{
    if ((master == NULL) || (master->last_report_valid == 0U))
    {
        return NULL;
    }

    return &master->last_report;
}

bus_master_report_t *get_master_report_obj(void)
{
    return &s_master_report;
}
