#ifndef __BUS_MASTER_H__
#define __BUS_MASTER_H__

#include "stdint.h"
#include "stdbool.h"
#include "string.h"
#include "pan_rf.h"
#include "rs485bsp.h"
#include "bus_rand.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BUS_MASTER_MAX_SLAVES         100

#define BUS_FRAME_HEAD_QUERY          0xA5
#define BUS_FRAME_HEAD_REPLY          0x5A

#define BUS_CMD_QUERY_STATUS          0x01
#define BUS_CMD_STATUS_REPLY          0x81

#define BUS_QUERY_FRAME_LEN           6
#define BUS_REPLY_FRAME_LEN           10

#define BUS_NODE_STATE_UNKNOWN        0
#define BUS_NODE_STATE_ONLINE         1
#define BUS_NODE_STATE_OFFLINE        2

#define BUS_MASTER_DEFAULT_CONTEND_M          80U
#define BUS_MASTER_CAD_IDLE_MS                60U
#define BUS_MASTER_CONTEND_SLOT_MS            5U
#define BUS_MASTER_ROUND_SLEEP_MS             1000U

typedef enum
{
    BUS_MASTER_IDLE = 0,
    BUS_MASTER_SEND_QUERY,
    BUS_MASTER_WAIT_TX_DONE,
    BUS_MASTER_WAIT_REPLY,

    /* 新增：竞争/休眠状态 */
    BUS_MASTER_WAIT_CAD_IDLE,
    BUS_MASTER_WAIT_CONTEND_SLOT,
    BUS_MASTER_SLEEP_AFTER_ROUND
} bus_master_state_t;

typedef struct
{
    uint16_t full_id;          // 高8位车辆号，低8位座位号
    uint8_t  vehicle_id;
    uint8_t  seat_id;

    uint8_t  sensor[4];        // 四个传感器状态
    int8_t   rssi;
    float    snr;

    uint32_t last_ok_tick;     // 最近一次成功收到应答的时刻
    uint32_t last_query_tick;  // 最近一次发起查询的时刻

    uint16_t success_count;
    uint16_t fail_count;
    uint8_t  lost_count;       // 连续丢包次数
    uint8_t  state;            // UNKNOWN / ONLINE / OFFLINE
} bus_slave_info_t;

typedef struct
{
    uint8_t vehicle_id;

    uint8_t poll_start_slave;
    uint8_t poll_end_slave;
		uint8_t round_first_slave;

    uint8_t current_slave;
    uint8_t expected_seq;

    uint8_t retry_count;
    uint8_t max_retry;

    uint32_t reply_timeout_ms;
    uint32_t inter_query_gap_ms;

    uint32_t last_tx_start_tick;     // 最近一次发送起点
    uint32_t last_tx_duration_ms;    // 最近一次TX耗时

    uint32_t state_tick;
    bus_master_state_t state;

    /* 新增：竞争相关变量 */
    uint8_t  contend_M;                  // 竞争窗口时隙数，默认80
    uint32_t cad_idle_start_tick;        // CAD开始连续空闲的时刻
    uint32_t contend_target_tick;        // 本次选中的竞争时隙到期时刻
    uint32_t round_sleep_until_tick;     // 一轮完成后的休眠截止时刻
		
		uint32_t cad_idle_ms;
		uint32_t round_sleep_ms;

    bool     channel_owned;              // 已成功抢到本轮信道
    bool     round_active;               // 当前是否处于本轮轮询中
    uint8_t  collision_count;            // 记录因首座位超时判定的碰撞次数

    bus_slave_info_t slaves[BUS_MASTER_MAX_SLAVES];
} bus_master_t;

void bus_master_init(bus_master_t *master);


void bus_master_start(bus_master_t *master);
void bus_master_task(bus_master_t *master, uint32_t now_ms);

void bus_master_on_tx_done(bus_master_t *master, uint32_t now_ms);
void bus_master_on_rx_done(bus_master_t *master, uint32_t now_ms);

const bus_slave_info_t *bus_master_get_slave(const bus_master_t *master, uint8_t seat_id);
bus_master_t *get_master_obj(void);

#ifdef __cplusplus
}
#endif

#endif
