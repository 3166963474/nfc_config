#include "bus_master.h"
#include "app_st25dv.h"
#include <stdarg.h>
/* 你已有的全局接收结构体，定义在 pan_rf.c */
extern struct RxDoneMsg RxDoneParams;

static uint8_t bus_checksum_xor(const uint8_t *buf, uint8_t len)
{
    uint8_t x = 0;
    uint8_t i;
    for (i = 0; i < len; i++)
    {
        x ^= buf[i];
    }
    return x;
}

static uint16_t bus_make_full_id(uint8_t vehicle_id, uint8_t seat_id)
{
    return ((uint16_t)vehicle_id << 8) | seat_id;
}

static bool bus_is_seat_in_range(const bus_master_t *master, uint8_t seat_id)
{
    return (seat_id >= master->poll_start_slave) && (seat_id <= master->poll_end_slave);
}

static void bus_advance_to_next_seat(bus_master_t *master)
{
    if (master->current_slave < master->poll_start_slave || master->current_slave > master->poll_end_slave)
    {
        master->current_slave = master->poll_start_slave;
        return;
    }

    if (master->current_slave >= master->poll_end_slave)
    {
        master->current_slave = master->poll_start_slave;
    }
    else
    {
        master->current_slave++;
    }
}

static void bus_enter_rx_mode(void)
{
    rf_set_mode(RF_MODE_STB3);
    rf_enter_continous_rx();
}

static bool bus_cad_is_idle(void)
{
    /* 依赖现有驱动中的 CAD 判空接口 */
    uint32_t chirp_time = rf_get_chirp_time(DEFAULT_BW,DEFAULT_SF);
    return (check_cad_rx_inactive(chirp_time) == LEVEL_INACTIVE);
}

static void bus_reset_for_new_round(bus_master_t *master)
{
		master->round_first_slave = master->poll_start_slave 
															+ bus_rand_slot(
																master->poll_end_slave - master->poll_start_slave +1
															);
    master->current_slave = master->round_first_slave;
	if(UART_DEBUG ==1){
	UART_DBG_Printf_DMA("first slave: %d \r\n",master->round_first_slave);
	}
    master->retry_count = 0;
    master->channel_owned = false;
    master->round_active = false;
}

static void bus_enter_wait_cad_idle(bus_master_t *master, uint32_t now_ms)
{
    master->state = BUS_MASTER_WAIT_CAD_IDLE;
    master->state_tick = now_ms;
    master->cad_idle_start_tick = 0;
    master->contend_target_tick = 0;
    master->retry_count = 0;
    master->channel_owned = false;
    master->round_active = false;
		rf_set_mode(RF_MODE_STB3);
    bus_enter_rx_mode();
}
static void bus_start_contend_backoff(bus_master_t *master, uint32_t now_ms)
{
		if(UART_DEBUG ==1){
		UART_DBG_Printf_DMA("Start contend\r\n");
		}
    uint8_t slot_idx = bus_rand_slot(master->contend_M);
    if(UART_DEBUG == 1){
		UART_DBG_Printf_DMA("slop_idx %d\r\n",slot_idx);
		}
    master->contend_target_tick = now_ms + ((uint32_t)slot_idx * BUS_MASTER_CONTEND_SLOT_MS);
    master->state = BUS_MASTER_WAIT_CONTEND_SLOT;
    master->state_tick = now_ms;
}

static bool bus_send_query(bus_master_t *master, uint8_t seat_id, uint32_t now_ms)
{
    uint8_t frame[BUS_QUERY_FRAME_LEN];
    uint16_t full_id = bus_make_full_id(master->vehicle_id, seat_id);

    (void)full_id;

    frame[0] = BUS_FRAME_HEAD_QUERY;
    frame[1] = BUS_CMD_QUERY_STATUS;
    frame[2] = master->vehicle_id;
    frame[3] = seat_id;
    frame[4] = master->expected_seq;
    frame[5] = bus_checksum_xor(frame, 5);

    rf_set_mode(RF_MODE_STB3);
    rf_enter_continous_tx();
    rf_continous_tx_send_data(frame, BUS_QUERY_FRAME_LEN);

    master->slaves[seat_id].last_query_tick = now_ms;
    master->state = BUS_MASTER_WAIT_TX_DONE;
    master->state_tick = now_ms;
    master->last_tx_start_tick = now_ms;
    return true;
}

static void bus_mark_current_failed(bus_master_t *master)
{
    bus_slave_info_t *slave = &master->slaves[master->current_slave];

    slave->fail_count++;
    slave->lost_count++;

    if (slave->lost_count >= master->max_retry)
    {
        slave->state = BUS_NODE_STATE_OFFLINE;
    }
}

static void bus_mark_reply_ok(bus_master_t *master,
                              uint8_t seat_id,
                              const uint8_t sensor[4],
                              int8_t rssi,
                              float snr,
                              uint32_t now_ms)
{
    bus_slave_info_t *slave = &master->slaves[seat_id];

    slave->sensor[0] = sensor[0];
    slave->sensor[1] = sensor[1];
    slave->sensor[2] = sensor[2];
    slave->sensor[3] = sensor[3];

    slave->rssi = rssi;
    slave->snr = snr;
    slave->last_ok_tick = now_ms;
    slave->success_count++;
    slave->lost_count = 0;
    slave->state = BUS_NODE_STATE_ONLINE;
}

static bool bus_parse_reply(bus_master_t *master,
                            const uint8_t *buf,
                            uint16_t len,
                            uint8_t *seat_id_out,
                            uint8_t sensor[4],
                            uint8_t *seq_out)
{
    uint8_t chk;

    if (len != BUS_REPLY_FRAME_LEN)
    {
        return false;
    }

    if (buf[0] != BUS_FRAME_HEAD_REPLY)
    {
        return false;
    }

    if (buf[1] != BUS_CMD_STATUS_REPLY)
    {
        return false;
    }

    if (buf[2] != master->vehicle_id)
    {
        return false;
    }

    if (!bus_is_seat_in_range(master, buf[3]))
    {
        return false;
    }

    chk = bus_checksum_xor(buf, BUS_REPLY_FRAME_LEN - 1);
    if (chk != buf[BUS_REPLY_FRAME_LEN - 1])
    {
        return false;
    }

    *seat_id_out = buf[3];
    sensor[0] = buf[4];
    sensor[1] = buf[5];
    sensor[2] = buf[6];
    sensor[3] = buf[7];
    *seq_out = buf[8];

    return true;
}

void bus_master_init(bus_master_t *master)
{
		master_payload_t *master_config_obj;
		master_config_obj = (master_payload_t *)payload_GetObject();
    uint16_t i;

    memset(master, 0, sizeof(*master));

    master->vehicle_id = master_config_obj->vehicle_id;
    master->poll_start_slave = master_config_obj->poll_start_slave;
    master->poll_end_slave = master_config_obj->poll_end_slave;
    master->current_slave = master_config_obj->poll_start_slave;

    master->expected_seq = 0;
    master->retry_count = 0;
    master->max_retry = master_config_obj->max_retry;

    master->reply_timeout_ms = master_config_obj->reply_timeout_ms;
    master->inter_query_gap_ms = master_config_obj->inter_query_gap_ms;

    master->contend_M = master_config_obj->contend_M;
    master->cad_idle_start_tick = 0;
    master->contend_target_tick = 0;
    master->round_sleep_until_tick = 0;

		master->cad_idle_ms = master_config_obj->cad_idle_ms;
		master->round_sleep_ms = master_config_obj->round_sleep_ms;

    master->channel_owned = false;
    master->round_active = false;
    master->collision_count = 0;

    master->state = BUS_MASTER_WAIT_CAD_IDLE;
    master->state_tick = 0;

    for (i = 0; i < BUS_MASTER_MAX_SLAVES; i++)
    {
        master->slaves[i].vehicle_id = master->vehicle_id;
        master->slaves[i].seat_id = (uint8_t)i;
        master->slaves[i].full_id = bus_make_full_id(master->vehicle_id, (uint8_t)i);
        master->slaves[i].state = BUS_NODE_STATE_UNKNOWN;
    }
}

void bus_master_start(bus_master_t *master)
{
    bus_reset_for_new_round(master);
    master->state = BUS_MASTER_WAIT_CAD_IDLE;
    master->state_tick = 0;
    master->cad_idle_start_tick = 0;
    master->contend_target_tick = 0;
    master->round_sleep_until_tick = 0;
    bus_enter_rx_mode();
}

void bus_master_on_tx_done(bus_master_t *master, uint32_t now_ms)
{
    master->last_tx_duration_ms = now_ms - master->last_tx_start_tick;
    rf_set_transmit_flag(RADIO_FLAG_IDLE);

    if (master->state == BUS_MASTER_WAIT_TX_DONE)
    {
        bus_enter_rx_mode();
        master->state = BUS_MASTER_WAIT_REPLY;
        master->state_tick = now_ms;
    }
}

void bus_master_on_rx_done(bus_master_t *master, uint32_t now_ms)
{
    uint8_t seat_id;
    uint8_t sensor[4];
    uint8_t seq;
    bool ok;
    uint8_t replied_seat;

    rf_set_recv_flag(RADIO_FLAG_IDLE);

    ok = bus_parse_reply(master,
                         RxDoneParams.Payload,
                         RxDoneParams.Size,
                         &seat_id,
                         sensor,
                         &seq);

    RxDoneParams.Size = 0;

    if (!ok)
    {
        return;
    }

    if (seat_id != master->current_slave)
    {
        return;
    }

    if (seq != master->expected_seq)
    {
        return;
    }

    bus_mark_reply_ok(master, seat_id, sensor, RxDoneParams.Rssi, RxDoneParams.Snr, now_ms);

    replied_seat = master->current_slave;

    /* 第一台座位回复成功，认为本轮已成功占用信道 */
    if (replied_seat == master->round_first_slave)
    {
        master->channel_owned = true;
        master->round_active = true;
    }

    master->retry_count = 0;
    master->expected_seq++;

    bus_advance_to_next_seat(master);
		
    if (master->channel_owned && master->current_slave == master->round_first_slave)
    {
        /* 一轮轮询完成，进入4分钟休眠 */
        master->state = BUS_MASTER_SLEEP_AFTER_ROUND;
        master->state_tick = now_ms;
        master->round_sleep_until_tick = now_ms + master->round_sleep_ms;
        bus_enter_rx_mode();
        return;
    }
    master->state = BUS_MASTER_IDLE;
    master->state_tick = now_ms;
}
_Bool Send_state=1;

/* 小段发送缓冲区大小，单个slave一段足够 */
#define UART1_JSON_SEG_BUF_SIZE   192

static HAL_StatusTypeDef uart1_send_json_segment(const char *fmt, ...)
{
    static char buf[UART1_JSON_SEG_BUF_SIZE];
    va_list args;
    int len;

    va_start(args, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* 格式化失败 */
    if (len < 0)
    {
        return HAL_ERROR;
    }

    /* 缓冲区不够，避免发送截断JSON */
    if (len >= (int)sizeof(buf))
    {
        return HAL_ERROR;
    }

    return UART1_SendBuf_DMA((uint8_t *)buf, (uint16_t)len);
}

void print_sensor_status(bus_master_t *master)
{
    if (master == NULL) return;

    uint8_t start = master->poll_start_slave;
    uint8_t end   = master->poll_end_slave;

    if (start > end) return;

    /* 先发头 */
    if (uart1_send_json_segment("{\"car_no\":\"%u\",\"slaves\":[",
                                master->vehicle_id) != HAL_OK)
    {
        return;
    }

    for (uint8_t slave_no = start; slave_no <= end; slave_no++)
    {
        uint8_t idx = (uint8_t)(slave_no);
        if (idx >= BUS_MASTER_MAX_SLAVES)
        {
            break;
        }

        bus_slave_info_t *slave = &master->slaves[idx];

        /* 传感器映射 */
        uint8_t seat0_belt = slave->sensor[2] ? 1 : 0;  /* sense3 */
        uint8_t seat0_seat = slave->sensor[3] ? 1 : 0;  /* sense4 */
        uint8_t seat1_belt = slave->sensor[1] ? 1 : 0;  /* sense2 */
        uint8_t seat1_seat = slave->sensor[0] ? 1 : 0;  /* sense1 */

        /* 在线状态：ONLINE=1，其余=0 */
        uint8_t status = (slave->state == BUS_NODE_STATE_ONLINE) ? 1 : 0;

        if (uart1_send_json_segment(
                "%s{\"slave_no\":%u,\"status\":%u,"
                "\"seat_0\":{\"belt_status\":%u,\"seat_status\":%u},"
                "\"seat_1\":{\"belt_status\":%u,\"seat_status\":%u}}",
                (slave_no == start) ? "" : ",",
                slave_no,
                status,
                seat0_belt, seat0_seat,
                seat1_belt, seat1_seat) != HAL_OK)
        {
            return;
        }
    }

    /* 最后发尾 */
    (void)uart1_send_json_segment("]}\r\n");
}

void bus_master_task(bus_master_t *master, uint32_t now_ms)
{
    if (rf_get_transmit_flag() == RADIO_FLAG_TXDONE)
    {
        bus_master_on_tx_done(master, now_ms);
    }

    if (rf_get_recv_flag() == RADIO_FLAG_RXDONE)
    {
        bus_master_on_rx_done(master, now_ms);
    }
    else if (rf_get_recv_flag() == RADIO_FLAG_RXERR)
    {
        rf_set_recv_flag(RADIO_FLAG_IDLE);
    }
    else if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT)
    {
        rf_set_recv_flag(RADIO_FLAG_IDLE);
    }

    switch (master->state)
    {
    case BUS_MASTER_WAIT_CAD_IDLE:
        if (bus_cad_is_idle())
        {
            if (master->cad_idle_start_tick == 0U)
            {
							if(UART_DEBUG == 1){
							UART_DBG_Printf_DMA("Start wait cad\r\n");
							}
              master->cad_idle_start_tick = now_ms;
            }
            else if ((now_ms - master->cad_idle_start_tick) >= master->cad_idle_ms)
            {
              bus_start_contend_backoff(master, now_ms);
							master->cad_idle_start_tick = 0U;
            }
        }
        else
        {
            master->cad_idle_start_tick = 0U;
        }
        break;

    case BUS_MASTER_WAIT_CONTEND_SLOT:
        if (!bus_cad_is_idle())
        {
					if(UART_DEBUG == 1){
					UART_DBG_Printf_DMA("Contend fail %d\r\n",now_ms);
					}
            bus_enter_wait_cad_idle(master, now_ms);
            break;
        }

        if ((int32_t)(now_ms - master->contend_target_tick) >= 0)
        {
					if(UART_DEBUG == 1){
					UART_DBG_Printf_DMA("Contend success\r\n");
					}
            master->retry_count = 0;
            master->state = BUS_MASTER_SEND_QUERY;
            master->state_tick = now_ms;
        }
        break;

    case BUS_MASTER_IDLE:
        if ((now_ms - master->state_tick) >= master->inter_query_gap_ms)
        {
            master->state = BUS_MASTER_SEND_QUERY;
            master->state_tick = now_ms;
        }
        break;

    case BUS_MASTER_SEND_QUERY:
        bus_send_query(master, master->current_slave, now_ms);
        break;

    case BUS_MASTER_WAIT_TX_DONE:
        if ((now_ms - master->state_tick) > 500U)
        {
            if (master->retry_count < master->max_retry)
            {
                master->retry_count++;
                master->state = BUS_MASTER_IDLE;
                master->state_tick = now_ms;
            }
            else
            {
                /* 如果连第一包都没正常发完，也回到争用前状态 */
                master->collision_count++;
                bus_reset_for_new_round(master);
                bus_enter_wait_cad_idle(master, now_ms);
            }
        }
        break;

    case BUS_MASTER_WAIT_REPLY:
        if ((now_ms - master->state_tick) >= master->reply_timeout_ms)
        {
            bus_mark_current_failed(master);

            if (!master->channel_owned && (master->current_slave == master->round_first_slave))
            {
                /* 还没抢到信道，第一座位超时，认为可能发生了碰撞 */
                if (master->retry_count < master->max_retry)
                {
                    master->retry_count++;
                    master->state = BUS_MASTER_SEND_QUERY;
                    master->state_tick = now_ms;
										UART_DBG_Printf_DMA("Collision detected\r\n");
                }
                else
                {
                    master->collision_count++;
                    bus_reset_for_new_round(master);
                    bus_start_contend_backoff(master, now_ms);
                }
            }
            else
            {
                /* 已占用信道后的普通轮询逻辑，保持原有风格 */
                if (master->retry_count < master->max_retry)
                {
                    master->retry_count++;
                    master->state = BUS_MASTER_SEND_QUERY;
                    master->state_tick = now_ms;
                }
                else
                {
                    master->retry_count = 0;
                    master->expected_seq++;
                    bus_advance_to_next_seat(master);

                    if (master->current_slave == master->round_first_slave)
                    {
                        /* 理论上走到这里意味着本轮结束 */
                        bus_reset_for_new_round(master);
                        master->state = BUS_MASTER_SLEEP_AFTER_ROUND;
                        master->state_tick = now_ms;
                        master->round_sleep_until_tick = now_ms + master->round_sleep_ms;
                    }
                    else
                    {
                        master->state = BUS_MASTER_SEND_QUERY;
                        master->state_tick = now_ms;
                    }
                }
            }
        }
        break;

    case BUS_MASTER_SLEEP_AFTER_ROUND:
			
				if(Send_state)
				{
					print_sensor_status(master);
					Send_state=0;
					UART_DBG_Printf_DMA("Report status\r\n");
				}
        if ((int32_t)(now_ms - master->round_sleep_until_tick) >= 0)
        {
						Send_state=1;
            bus_reset_for_new_round(master);
            bus_enter_wait_cad_idle(master, now_ms);
        }
        break;

    default:
        bus_reset_for_new_round(master);
        bus_enter_wait_cad_idle(master, now_ms);
        break;
    }
}

const bus_slave_info_t *bus_master_get_slave(const bus_master_t *master, uint8_t seat_id)
{
    return &master->slaves[seat_id];
}
