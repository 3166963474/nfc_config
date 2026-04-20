#ifndef APP_ST25DV_H
#define APP_ST25DV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "st25dv.h"
#include "pan_rf.h"
#include "bus_master.h"

#define FRAME_SOF                    0xAA
#define FRAME_MASTER_TYPE            0x01
#define FRAME_SLAVE_TYPE             0x02

#define RESERVED_LEN                 16U
#define RESERVED_VALID_MASK          0x01U

#define FRAME_OFFSET_SOF             0U
#define FRAME_OFFSET_TIME            1U
#define FRAME_OFFSET_TYPE            5U
#define FRAME_OFFSET_PAYLOAD         6U

/* payload = rf(8) + reserved(16) + business fields */

#define FRAME_CRC_LEN                4U
#define FRAME_TOTAL_LEN(frame_len)   ((uint16_t)((frame_len) + FRAME_CRC_LEN))

#define RESERVED_OFFSET_IN_PAYLOAD   8U
#define RESERVED_OFFSET_IN_FRAME     ((uint16_t)(FRAME_OFFSET_PAYLOAD + RESERVED_OFFSET_IN_PAYLOAD))

#define FRAME_MASTER_PAYLOAD_LEN     45U
#define FRAME_SLAVE_PAYLOAD_LEN      26U

/* len includes SOF + TIME + TYPE + PAYLOAD, excludes CRC */
#define FRAME_MASTER_LEN             ((uint16_t)(FRAME_OFFSET_PAYLOAD + FRAME_MASTER_PAYLOAD_LEN))
#define FRAME_SLAVE_LEN              ((uint16_t)(FRAME_OFFSET_PAYLOAD + FRAME_SLAVE_PAYLOAD_LEN))

/* ================= 业务范围定义 ================= */

#define RF_FREQ_MIN_HZ                 405000000UL
#define RF_FREQ_MAX_HZ                 1080000000UL

/* 这些按你的业务自行调整 */
#define VEHICLE_ID_MIN                 0x00U
#define VEHICLE_ID_MAX                 0xFFU

#define MASTER_MAX_RETRY_MIN           0U
#define MASTER_MAX_RETRY_MAX           10U

#define MASTER_REPLY_TIMEOUT_MS_MIN    1UL
#define MASTER_REPLY_TIMEOUT_MS_MAX    500UL

#define MASTER_INTER_QUERY_GAP_MS_MIN  0UL
#define MASTER_INTER_QUERY_GAP_MS_MAX  50UL

#define MASTER_CONTEND_M_MIN           1U
#define MASTER_CONTEND_M_MAX           255U

#define MASTER_CAD_IDLE_MS_MIN         0UL
#define MASTER_CAD_IDLE_MS_MAX         600UL

#define MASTER_ROUND_SLEEP_MS_MIN      0UL
#define MASTER_ROUND_SLEEP_MS_MAX      600000UL

typedef enum
{
    PAYLOAD_CHECK_OK = 0,

    PAYLOAD_CHECK_ERR_NULL = -1,

    PAYLOAD_CHECK_ERR_RF_PWR = -2,
    PAYLOAD_CHECK_ERR_RF_FREQ = -3,
    PAYLOAD_CHECK_ERR_RF_SF = -4,
    PAYLOAD_CHECK_ERR_RF_BW = -5,
    PAYLOAD_CHECK_ERR_RF_CR = -6,

    PAYLOAD_CHECK_ERR_VEHICLE_ID = -10,

    PAYLOAD_CHECK_ERR_POLL_START_SEAT = -11,
    PAYLOAD_CHECK_ERR_POLL_END_SEAT = -12,
    PAYLOAD_CHECK_ERR_POLL_RANGE = -13,

    PAYLOAD_CHECK_ERR_MAX_RETRY = -14,
    PAYLOAD_CHECK_ERR_REPLY_TIMEOUT = -15,
    PAYLOAD_CHECK_ERR_INTER_QUERY_GAP = -16,
    PAYLOAD_CHECK_ERR_CONTEND_M = -17,
    PAYLOAD_CHECK_ERR_CAD_IDLE_MS = -18,
    PAYLOAD_CHECK_ERR_ROUND_SLEEP_MS = -19,

    PAYLOAD_CHECK_ERR_SEAT_ID = -20
} payload_check_result_t;

#pragma pack(push, 1)

typedef struct
{
    uint8_t  pwr;
    uint32_t freq;
    uint8_t  sf;
    uint8_t  bw;
    uint8_t  cr;
} rf_param_t;

typedef struct
{
    rf_param_t rf_param;
    uint8_t reserved[RESERVED_LEN];
    uint8_t vehicle_id;
    uint8_t poll_start_slave;
    uint8_t poll_end_slave;
    uint8_t max_retry;
    uint32_t reply_timeout_ms;
    uint32_t inter_query_gap_ms;
    uint8_t contend_M;
    uint32_t cad_idle_ms;
    uint32_t round_sleep_ms;
} master_payload_t;

typedef struct
{
    rf_param_t rf_param;
    uint8_t reserved[RESERVED_LEN];
    uint8_t vehicle_id;
    uint8_t seat_id;
} slave_payload_t;

#pragma pack(pop)

/* 应用层返回码 */
typedef enum
{
    APP_ST25DV_OK = 0,
    APP_ST25DV_ERROR = -1,
    APP_ST25DV_INVALID_PARAM = -2,
    APP_ST25DV_NOT_READY = -3
} APP_ST25DV_Status_t;

uint32_t *payload_GetObject(void);
APP_ST25DV_Status_t APP_pragma_init(void);
APP_ST25DV_Status_t is_frame_valid(const uint8_t *frame_buf);
payload_check_result_t check_slave_payload(const slave_payload_t *slave);
APP_ST25DV_Status_t APP_ST25DV_SaveCurrentPayload(uint32_t unix_time);
void nfc_task_in_tim(void);


/* 全局初始化与状态 */
APP_ST25DV_Status_t APP_ST25DV_Init(void);
APP_ST25DV_Status_t APP_ST25DV_DeInit(void);
uint8_t APP_ST25DV_IsInitialized(void);

/* 基本信息 */
APP_ST25DV_Status_t APP_ST25DV_ReadId(uint8_t *pId);
APP_ST25DV_Status_t APP_ST25DV_ReadIcRev(uint8_t *pRev);
APP_ST25DV_Status_t APP_ST25DV_ReadUid(ST25DV_UID *pUid);
payload_check_result_t check_master_payload(const master_payload_t *master);

/* 用户区读写 */
APP_ST25DV_Status_t APP_ST25DV_ReadBytes(uint16_t addr, uint8_t *pData, uint16_t len);
APP_ST25DV_Status_t APP_ST25DV_WriteBytes(uint16_t addr, const uint8_t *pData, uint16_t len);

/* 系统寄存器读写 */
APP_ST25DV_Status_t APP_ST25DV_ReadRegister(uint16_t reg, uint8_t *pData, uint16_t len);
APP_ST25DV_Status_t APP_ST25DV_WriteRegister(uint16_t reg, const uint8_t *pData, uint16_t len);

/* 设备就绪检测 */
APP_ST25DV_Status_t APP_ST25DV_IsDeviceReady(uint32_t trials);

/* I2C 密码相关 */
APP_ST25DV_Status_t APP_ST25DV_PresentPassword(uint32_t msb, uint32_t lsb);

/* 获取底层对象，便于你后续直接调用 ST25DV 扩展接口 */
ST25DV_Object_t *APP_ST25DV_GetObject(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_ST25DV_H */
