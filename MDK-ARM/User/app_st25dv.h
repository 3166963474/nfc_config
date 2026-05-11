#ifndef APP_ST25DV_H
#define APP_ST25DV_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "st25dv.h"
#include "pan_rf.h"

#define FRAME_SOF                    0xAAU
#define FRAME_MASTER_TYPE            0x01U
#define FRAME_SLAVE_TYPE             0x02U

#define RESERVED_LEN                 16U

#define FRAME_OFFSET_SOF             0U
#define FRAME_OFFSET_TIME            1U
#define FRAME_OFFSET_TYPE            5U
#define FRAME_OFFSET_PAYLOAD         6U

/* payload = rf(8) + reserved(16) + business fields */
#define FRAME_CRC_LEN                4U
#define FRAME_TOTAL_LEN(frame_len)   ((uint16_t)((frame_len) + FRAME_CRC_LEN))

#define RESERVED_OFFSET_IN_PAYLOAD   8U
#define RESERVED_OFFSET_IN_FRAME     ((uint16_t)(FRAME_OFFSET_PAYLOAD + RESERVED_OFFSET_IN_PAYLOAD))

/* Current frame_builder_v2 payload sizes. */
#define FRAME_MASTER_PAYLOAD_LEN     29U
#define FRAME_SLAVE_PAYLOAD_LEN      42U

/* len includes SOF + TIME + TYPE + PAYLOAD, excludes CRC */
#define FRAME_MASTER_LEN             ((uint16_t)(FRAME_OFFSET_PAYLOAD + FRAME_MASTER_PAYLOAD_LEN))
#define FRAME_SLAVE_LEN              ((uint16_t)(FRAME_OFFSET_PAYLOAD + FRAME_SLAVE_PAYLOAD_LEN))
#define FRAME_MASTER_TOTAL_LEN       FRAME_TOTAL_LEN(FRAME_MASTER_LEN)
#define FRAME_SLAVE_TOTAL_LEN        FRAME_TOTAL_LEN(FRAME_SLAVE_LEN)
#define APP_ST25DV_FRAME_MAX_LEN     FRAME_SLAVE_TOTAL_LEN

/* ================= Business range definitions ================= */

#define RF_FREQ_MIN_HZ                 405000000UL
#define RF_FREQ_MAX_HZ                 1080000000UL

#define VEHICLE_ID_MIN                 0x00U
#define VEHICLE_ID_MAX                 0xFFU

/* RS485 feature flags. These are independent enables, not exclusive modes. */
#define RS485_FEATURE_NONE             0x00U
#define RS485_FEATURE_JSON_OUT         0x01U
#define RS485_FEATURE_FORWARD_TO_OTHER 0x02U
#define RS485_FEATURE_LOG_OUT         0x04U
#define RS485_FEATURE_VALID_MASK       0x0FU

#define UART_BAUDRATE_INDEX_MAX        14U

#define SEAT_PORT_COUNT                2U
#define SEAT_ENABLE_FALSE              0U
#define SEAT_ENABLE_TRUE               1U
#define SEAT_NO_MIN                    1U
#define SEAT_NO_MAX                    255U

/* bit0 -> order_state 1, bit11 -> order_state 12 */
#define ORDER_STATE_COUNT              12U
#define ORDER_STATE_MASK_VALID         0x0FFFU
#define FILTER_TIME_S_MAX              200U

#define SLAVE_SLOT_MS_MIN              1U
#define SLAVE_SLOT_MS_MAX              100U
#define SLAVE_CONTEND_N_MIN            1U
#define SLAVE_CONTEND_N_MAX            255U
#define SLAVE_IDLE_CONFIRM_MS_MIN      1U
#define SLAVE_IDLE_CONFIRM_MS_MAX      255U
#define SLAVE_ACK_TIMEOUT_MS_MIN       1U
#define SLAVE_ACK_TIMEOUT_MS_MAX       5000U

/* 255 means retry forever. Other values are concrete retry counts. */
#define SLAVE_MAX_RETRY_FOREVER        255U

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

    PAYLOAD_CHECK_ERR_RS485_BAUD = -11,
    PAYLOAD_CHECK_ERR_RS485_FEATURE = -12,

    PAYLOAD_CHECK_ERR_SEAT_ENABLE = -20,
    PAYLOAD_CHECK_ERR_SEAT_NO = -21,
    PAYLOAD_CHECK_ERR_SEAT_DUPLICATE = -22,
    PAYLOAD_CHECK_ERR_ORDER_MASK = -23,
    PAYLOAD_CHECK_ERR_FILTER_TIME = -24,

    PAYLOAD_CHECK_ERR_SLOT_MS = -30,
    PAYLOAD_CHECK_ERR_CONTEND_N = -31,
    PAYLOAD_CHECK_ERR_IDLE_CONFIRM_MS = -32,
    PAYLOAD_CHECK_ERR_ACK_TIMEOUT_MS = -33
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
    uint8_t baudrate_index;
    uint8_t feature_flags;
} rs485_port_config_t;

typedef struct
{
    uint8_t enable;     /* 1: alarm and report enabled; 0: no alarm and no report. */
    uint8_t seat_no;    /* NFC configured seat number. 0 is invalid when enabled. */
} seat_port_config_t;

typedef struct
{
    uint16_t abnormal_order_mask;  /* bit0 -> order_state 1, bit11 -> order_state 12. */
    uint16_t filter_order_mask;    /* bit0 -> order_state 1, bit11 -> order_state 12. */
    uint8_t  filter_time_s;        /* 0~15 s. */
    uint8_t  alarm_delay_s;        /* 0~255 s. */
    uint8_t  alarm_duration_s;     /* 0: disabled, 255: until next confirmed state. */
} seat_behavior_config_t;

typedef struct
{
    uint8_t  slot_ms;               /* Discrete contention slot length. Default 5 ms. */
    uint8_t  contend_slot_count_n;  /* random(N) returns 0~N-1. */
    uint8_t  idle_confirm_ms;       /* Continuous idle time before contention starts. */
    uint16_t ack_timeout_ms;        /* ACK timeout after TX end. */
    uint8_t  max_retry;             /* 255 means retry forever. */
} slave_contend_config_t;

typedef struct
{
    rf_param_t rf;
    uint8_t reserved[RESERVED_LEN];

    uint8_t vehicle_id;

    rs485_port_config_t rs485_1;
    rs485_port_config_t rs485_2;
} master_payload_t;

typedef struct
{
    rf_param_t rf;
    uint8_t reserved[RESERVED_LEN];

    uint8_t vehicle_id;

    seat_port_config_t seat[SEAT_PORT_COUNT];
    seat_behavior_config_t seat_behavior;
    slave_contend_config_t contend;
} slave_payload_t;

#pragma pack(pop)

/* Application return code */
typedef enum
{
    APP_ST25DV_OK = 0,
    APP_ST25DV_ERROR = -1,
    APP_ST25DV_INVALID_PARAM = -2,
    APP_ST25DV_NOT_READY = -3
} APP_ST25DV_Status_t;

extern slave_payload_t slave_payload;
extern master_payload_t master_payload;

uint32_t *payload_GetObject(void);
APP_ST25DV_Status_t APP_pragma_init(void);
APP_ST25DV_Status_t is_frame_valid(const uint8_t *frame_buf);
payload_check_result_t check_slave_payload(const slave_payload_t *slave);
payload_check_result_t check_master_payload(const master_payload_t *master);
APP_ST25DV_Status_t APP_ST25DV_SaveCurrentPayload(uint32_t unix_time);
void nfc_task_in_tim(void);

/* Global init and status */
APP_ST25DV_Status_t APP_ST25DV_Init(void);
APP_ST25DV_Status_t APP_ST25DV_DeInit(void);
uint8_t APP_ST25DV_IsInitialized(void);

/* Basic information */
APP_ST25DV_Status_t APP_ST25DV_ReadId(uint8_t *pId);
APP_ST25DV_Status_t APP_ST25DV_ReadIcRev(uint8_t *pRev);
APP_ST25DV_Status_t APP_ST25DV_ReadUid(ST25DV_UID *pUid);

/* User EEPROM read/write */
APP_ST25DV_Status_t APP_ST25DV_ReadBytes(uint16_t addr, uint8_t *pData, uint16_t len);
APP_ST25DV_Status_t APP_ST25DV_WriteBytes(uint16_t addr, const uint8_t *pData, uint16_t len);

/* System register read/write */
APP_ST25DV_Status_t APP_ST25DV_ReadRegister(uint16_t reg, uint8_t *pData, uint16_t len);
APP_ST25DV_Status_t APP_ST25DV_WriteRegister(uint16_t reg, const uint8_t *pData, uint16_t len);

/* Device ready detection */
APP_ST25DV_Status_t APP_ST25DV_IsDeviceReady(uint32_t trials);

/* I2C password */
APP_ST25DV_Status_t APP_ST25DV_PresentPassword(uint32_t msb, uint32_t lsb);

/* Get low-level driver object */
ST25DV_Object_t *APP_ST25DV_GetObject(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_ST25DV_H */
