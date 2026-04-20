#include "bus_slave.h"
#include "app_st25dv.h"
/* pan_rf.c 里定义的接收结果结构体 */
extern struct RxDoneMsg RxDoneParams;

/* =========================
 * 传感器输入电平定义
 * 根据你的硬件实际情况改这里：
 * 1 = 有效 / 0 = 无效
 * 如果你的传感器是低电平有效，就把 READ_SENSOR_PIN 改成取反
 * ========================= */
#define READ_SENSOR_PIN(port, pin) \
    ((HAL_GPIO_ReadPin((port), (pin)) == GPIO_PIN_SET) ? 1U : 0U)

/* 如果实际是低电平有效，请改成下面这一行 */
// #define READ_SENSOR_PIN(port, pin) \
//     ((HAL_GPIO_ReadPin((port), (pin)) == GPIO_PIN_RESET) ? 1U : 0U)

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

static void bus_slave_enter_rx(void)
{
    rf_set_mode(RF_MODE_STB3);
    rf_enter_continous_rx();
}

static void bus_slave_read_sensors(bus_slave_t *slave)
{
    slave->sensor_cache[0] = !READ_SENSOR_PIN(Senser_1_GPIO_Port, Senser_1_Pin);
    slave->sensor_cache[1] = !READ_SENSOR_PIN(Senser_2_GPIO_Port, Senser_2_Pin);
    slave->sensor_cache[2] = !READ_SENSOR_PIN(Senser_3_GPIO_Port, Senser_3_Pin);
    slave->sensor_cache[3] = !READ_SENSOR_PIN(Senser_4_GPIO_Port, Senser_4_Pin);
}

static bool bus_slave_parse_query(bus_slave_t *slave,
                                  const uint8_t *buf,
                                  uint16_t len,
                                  uint8_t *seq_out)
{
    uint8_t chk;

    if (len != BUS_QUERY_FRAME_LEN)
    {
        return false;
    }

    if (buf[0] != BUS_FRAME_HEAD_QUERY)
    {
        return false;
    }

    if (buf[1] != BUS_CMD_QUERY_STATUS)
    {
        return false;
    }

    chk = bus_checksum_xor(buf, BUS_QUERY_FRAME_LEN - 1);
    if (chk != buf[BUS_QUERY_FRAME_LEN - 1])
    {
        return false;
    }

    if (buf[2] != slave->vehicle_id)
    {
        return false;
    }

    if (buf[3] != slave->seat_id)
    {
        return false;
    }

    *seq_out = buf[4];
    return true;
}

static void bus_slave_send_reply(bus_slave_t *slave, uint8_t seq)
{
    uint8_t frame[BUS_REPLY_FRAME_LEN];

    bus_slave_read_sensors(slave);

    frame[0] = BUS_FRAME_HEAD_REPLY;
    frame[1] = BUS_CMD_STATUS_REPLY;
    frame[2] = slave->vehicle_id;
    frame[3] = slave->seat_id;
    frame[4] = slave->sensor_cache[0];
    frame[5] = slave->sensor_cache[1];
    frame[6] = slave->sensor_cache[2];
    frame[7] = slave->sensor_cache[3];
    frame[8] = seq;
    frame[9] = bus_checksum_xor(frame, BUS_REPLY_FRAME_LEN - 1);

    rf_set_mode(RF_MODE_STB3);
    rf_enter_continous_tx();
    rf_continous_tx_send_data(frame, BUS_REPLY_FRAME_LEN);

    slave->last_seq = seq;
    slave->tx_count++;
    slave->state = BUS_SLAVE_WAIT_TX_DONE;
}

void bus_slave_init(bus_slave_t *slave, uint8_t vehicle_id, uint8_t seat_id)
{
    memset(slave, 0, sizeof(*slave));
	
		slave_payload_t *slave_config_obj;
		slave_config_obj = (slave_payload_t *)payload_GetObject();
	
    slave->vehicle_id = slave_config_obj->vehicle_id;
    slave->seat_id = slave_config_obj->seat_id;
    slave->full_id = bus_make_full_id(vehicle_id, seat_id);
    slave->state = BUS_SLAVE_IDLE;
}

void bus_slave_start(bus_slave_t *slave)
{
    slave->state = BUS_SLAVE_IDLE;
    bus_slave_enter_rx();
}

void bus_slave_on_rx_done(bus_slave_t *slave)
{
    uint8_t seq;
    bool match_ok;

    rf_set_recv_flag(RADIO_FLAG_IDLE);

    match_ok = bus_slave_parse_query(slave,
                                     RxDoneParams.Payload,
                                     RxDoneParams.Size,
                                     &seq);

    /* 清空本次缓存，避免旧数据重复处理 */
    RxDoneParams.Size = 0;

    if (!match_ok)
    {
        slave->rx_invalid_count++;
        bus_slave_enter_rx();
        return;
    }

    slave->rx_ok_count++;
    bus_slave_send_reply(slave, seq);
}

void bus_slave_on_tx_done(bus_slave_t *slave)
{
    rf_set_transmit_flag(RADIO_FLAG_IDLE);

    /* 从机端不对发射失败/异常额外处理，统一回到接收 */
    slave->state = BUS_SLAVE_IDLE;
    bus_slave_enter_rx();
}

void bus_slave_task(bus_slave_t *slave)
{
    if (rf_get_recv_flag() == RADIO_FLAG_RXDONE)
    {
        bus_slave_on_rx_done(slave);
    }
    else if (rf_get_recv_flag() == RADIO_FLAG_RXERR)
    {
        rf_set_recv_flag(RADIO_FLAG_IDLE);
        bus_slave_enter_rx();
    }
    else if (rf_get_recv_flag() == RADIO_FLAG_RXTIMEOUT)
    {
        rf_set_recv_flag(RADIO_FLAG_IDLE);
        bus_slave_enter_rx();
    }

    if (rf_get_transmit_flag() == RADIO_FLAG_TXDONE)
    {
        bus_slave_on_tx_done(slave);
    }
}
