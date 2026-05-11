#include "app_st25dv.h"
#include "custom_bus.h"
#include <string.h>
#include "rs485bsp.h"
#include "flash_storage.h"

slave_payload_t slave_payload;
master_payload_t master_payload;

static uint16_t APP_ST25DV_GetFrameLenByType(uint8_t type)
{
    switch (type)
    {
        case FRAME_MASTER_TYPE:
            return FRAME_MASTER_LEN;

        case FRAME_SLAVE_TYPE:
            return FRAME_SLAVE_LEN;

        default:
            return 0U;
    }
}

static uint16_t APP_ST25DV_GetPayloadLenByType(uint8_t type)
{
    switch (type)
    {
        case FRAME_MASTER_TYPE:
            return FRAME_MASTER_PAYLOAD_LEN;

        case FRAME_SLAVE_TYPE:
            return FRAME_SLAVE_PAYLOAD_LEN;

        default:
            return 0U;
    }
}

static uint16_t APP_ST25DV_GetTotalFrameLenByType(uint8_t type)
{
    uint16_t frame_len;

    frame_len = APP_ST25DV_GetFrameLenByType(type);
    if (frame_len == 0U)
    {
        return 0U;
    }

    return FRAME_TOTAL_LEN(frame_len);
}

static APP_ST25DV_Status_t app_build_frame_from_current_payload(uint32_t unix_time,
                                                                uint8_t *frame_buf,
                                                                uint16_t frame_buf_size,
                                                                uint16_t *out_frame_len)
{
    uint8_t type;
    uint16_t payload_len;
    uint16_t frame_len_without_crc;
    uint16_t frame_len_total;
    uint32_t crc;

    if ((frame_buf == NULL) || (out_frame_len == NULL))
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    if (BUS_MASTER_DEF)
    {
        type = FRAME_MASTER_TYPE;
    }
    else
    {
        type = FRAME_SLAVE_TYPE;
    }

    payload_len = APP_ST25DV_GetPayloadLenByType(type);
    frame_len_without_crc = APP_ST25DV_GetFrameLenByType(type);
    frame_len_total = APP_ST25DV_GetTotalFrameLenByType(type);

    if ((payload_len == 0U) || (frame_len_without_crc == 0U) || (frame_len_total == 0U))
    {
        return APP_ST25DV_ERROR;
    }

    if (frame_buf_size < frame_len_total)
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    memset(frame_buf, 0, frame_buf_size);

    frame_buf[FRAME_OFFSET_SOF] = FRAME_SOF;
    memcpy(&frame_buf[FRAME_OFFSET_TIME], &unix_time, sizeof(unix_time));
    frame_buf[FRAME_OFFSET_TYPE] = type;

    if (type == FRAME_MASTER_TYPE)
    {
        memcpy(frame_buf + FRAME_OFFSET_PAYLOAD, &master_payload, payload_len);
    }
    else
    {
        memcpy(frame_buf + FRAME_OFFSET_PAYLOAD, &slave_payload, payload_len);
    }

    /* CRC32 over [SOF + TIME + TYPE + PAYLOAD]. */
    crc = storage_crc32(frame_buf, frame_len_without_crc);
    memcpy(frame_buf + frame_len_without_crc, &crc, sizeof(crc));

    *out_frame_len = frame_len_total;
    return APP_ST25DV_OK;
}

/* -------------------- 静态对象 -------------------- */

static ST25DV_Object_t s_st25dv_obj;
static uint8_t s_is_initialized = 0U;

/* -------------------- 底层适配函数 -------------------- */

static int32_t APP_ST25DV_BusInit(void)
{
    return BSP_I2C2_Init();
}

static int32_t APP_ST25DV_BusDeInit(void)
{
    return BSP_I2C2_DeInit();
}

static int32_t APP_ST25DV_BusIsReady(uint16_t devAddr, const uint32_t trials)
{
    return BSP_I2C2_IsReady(devAddr, trials);
}

static int32_t APP_ST25DV_BusWrite(uint16_t devAddr, uint16_t reg, const uint8_t *pData, uint16_t len)
{
    /* BSP 接口参数不是 const，这里做安全转接 */
    return BSP_I2C2_WriteReg16(devAddr, reg, (uint8_t *)pData, len);
}

static int32_t APP_ST25DV_BusRead(uint16_t devAddr, uint16_t reg, uint8_t *pData, uint16_t len)
{
    return BSP_I2C2_ReadReg16(devAddr, reg, pData, len);
}

static uint32_t APP_ST25DV_GetTickWrapper(void)
{
    return (uint32_t)BSP_GetTick();
}

/* -------------------- 内部工具函数 -------------------- */

static APP_ST25DV_Status_t APP_ST25DV_CheckReady(void)
{
    if (s_is_initialized == 0U)
    {
        return APP_ST25DV_NOT_READY;
    }
    return APP_ST25DV_OK;
}

static APP_ST25DV_Status_t APP_ST25DV_FromDrvStatus(int32_t ret)
{
    return (ret == 0) ? APP_ST25DV_OK : APP_ST25DV_ERROR;
}


APP_ST25DV_Status_t APP_ST25DV_ConfigPollEvents(void)
{
    ST25DV_Object_t *pObj;
    ST25DV_PASSWD pwd;
    uint16_t gpo_cfg;
    int32_t ret;

    pObj = APP_ST25DV_GetObject();
    if (pObj == NULL)
    {
        return APP_ST25DV_ERROR;
    }

    /* 默认 I2C 密码全 0 */
    pwd.MsbPasswd = 0x00000000UL;
    pwd.LsbPasswd = 0x00000000UL;

    ret = ST25DV_PresentI2CPassword(pObj, pwd);
    if (ret != 0)
    {
        UART_DBG_Printf_DMA("Present I2C password failed\r\n");
        return APP_ST25DV_ERROR;
    }

    /* 只打开 RF_WRITE 事件 */
    gpo_cfg = 0x0080U;

    ret = ST25DV_ConfigureGPO(pObj, gpo_cfg);
    if (ret != 0)
    {
        UART_DBG_Printf_DMA("ST25DV_ConfigureGPO failed\r\n");
        return APP_ST25DV_ERROR;
    }

    /* 动态打开 GPO 输出 */
    ret = ST25DV_SetGPO_en_Dyn(pObj);
    if (ret != 0)
    {
        UART_DBG_Printf_DMA("ST25DV_SetGPO_en_Dyn failed\r\n");
        return APP_ST25DV_ERROR;
    }
		if(UART_DEBUG ==1){
    UART_DBG_Printf_DMA("Poll event configured: RF_WRITE only\r\n");
		}
    return APP_ST25DV_OK;
}
/* -------------------- 对外接口实现 -------------------- */

APP_ST25DV_Status_t APP_ST25DV_Init(void)
{
    ST25DV_IO_t io_ctx;
    int32_t ret;

    if (s_is_initialized != 0U)
    {
        return APP_ST25DV_OK;
    }

    (void)memset(&s_st25dv_obj, 0, sizeof(s_st25dv_obj));
    (void)memset(&io_ctx, 0, sizeof(io_ctx));

    io_ctx.Init    = APP_ST25DV_BusInit;
    io_ctx.DeInit  = APP_ST25DV_BusDeInit;
    io_ctx.IsReady = APP_ST25DV_BusIsReady;
    io_ctx.Write   = APP_ST25DV_BusWrite;
    io_ctx.Read    = APP_ST25DV_BusRead;
    io_ctx.GetTick = APP_ST25DV_GetTickWrapper;

    ret = ST25DV_RegisterBusIO(&s_st25dv_obj, &io_ctx);
    if (ret != 0)
    {
        return APP_ST25DV_ERROR;
    }

    ST25DV_Init(&s_st25dv_obj);
		s_st25dv_obj.IsInitialized =1U;
    s_is_initialized = 1U;
		APP_ST25DV_ConfigPollEvents();
    return APP_ST25DV_OK;
}

APP_ST25DV_Status_t APP_ST25DV_DeInit(void)
{
    if (s_is_initialized == 0U)
    {
        return APP_ST25DV_OK;
    }

    if (s_st25dv_obj.IO.DeInit != NULL)
    {
        if (s_st25dv_obj.IO.DeInit() != 0)
        {
            return APP_ST25DV_ERROR;
        }
    }

    s_is_initialized = 0U;
    (void)memset(&s_st25dv_obj, 0, sizeof(s_st25dv_obj));

    return APP_ST25DV_OK;
}

uint8_t APP_ST25DV_IsInitialized(void)
{
    return s_is_initialized;
}

APP_ST25DV_Status_t APP_ST25DV_ReadId(uint8_t *pId)
{
    if (pId == NULL)
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    if (APP_ST25DV_CheckReady() != APP_ST25DV_OK)
    {
        return APP_ST25DV_NOT_READY;
    }

    return APP_ST25DV_FromDrvStatus(ST25DV_ReadID(&s_st25dv_obj, pId));
}

APP_ST25DV_Status_t APP_ST25DV_ReadIcRev(uint8_t *pRev)
{
    if (pRev == NULL)
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    if (APP_ST25DV_CheckReady() != APP_ST25DV_OK)
    {
        return APP_ST25DV_NOT_READY;
    }

    return APP_ST25DV_FromDrvStatus(ST25DV_ReadICRev(&s_st25dv_obj, pRev));
}

APP_ST25DV_Status_t APP_ST25DV_ReadUid(ST25DV_UID *pUid)
{
    if (pUid == NULL)
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    if (APP_ST25DV_CheckReady() != APP_ST25DV_OK)
    {
        return APP_ST25DV_NOT_READY;
    }

    return APP_ST25DV_FromDrvStatus(ST25DV_ReadUID(&s_st25dv_obj, pUid));
}

APP_ST25DV_Status_t APP_ST25DV_ReadBytes(uint16_t addr, uint8_t *pData, uint16_t len)
{
    if ((pData == NULL) || (len == 0U))
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    if (APP_ST25DV_CheckReady() != APP_ST25DV_OK)
    {
        return APP_ST25DV_NOT_READY;
    }

    return APP_ST25DV_FromDrvStatus(ST25DV_ReadData(&s_st25dv_obj, pData, addr, len));
}

APP_ST25DV_Status_t APP_ST25DV_WriteBytes(uint16_t addr, const uint8_t *pData, uint16_t len)
{
    if ((pData == NULL) || (len == 0U))
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    if (APP_ST25DV_CheckReady() != APP_ST25DV_OK)
    {
        return APP_ST25DV_NOT_READY;
    }

    return APP_ST25DV_FromDrvStatus(ST25DV_WriteData(&s_st25dv_obj, pData, addr, len));
}

APP_ST25DV_Status_t APP_ST25DV_ReadRegister(uint16_t reg, uint8_t *pData, uint16_t len)
{
    if ((pData == NULL) || (len == 0U))
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    if (APP_ST25DV_CheckReady() != APP_ST25DV_OK)
    {
        return APP_ST25DV_NOT_READY;
    }

    return APP_ST25DV_FromDrvStatus(ST25DV_ReadRegister(&s_st25dv_obj, pData, reg, len));
}

APP_ST25DV_Status_t APP_ST25DV_WriteRegister(uint16_t reg, const uint8_t *pData, uint16_t len)
{
    if ((pData == NULL) || (len == 0U))
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    if (APP_ST25DV_CheckReady() != APP_ST25DV_OK)
    {
        return APP_ST25DV_NOT_READY;
    }

    return APP_ST25DV_FromDrvStatus(ST25DV_WriteRegister(&s_st25dv_obj, pData, reg, len));
}

APP_ST25DV_Status_t APP_ST25DV_IsDeviceReady(uint32_t trials)
{
    if (APP_ST25DV_CheckReady() != APP_ST25DV_OK)
    {
        return APP_ST25DV_NOT_READY;
    }

    return APP_ST25DV_FromDrvStatus(ST25DV_IsDeviceReady(&s_st25dv_obj, trials));
}

APP_ST25DV_Status_t APP_ST25DV_PresentPassword(uint32_t msb, uint32_t lsb)
{
    ST25DV_PASSWD pwd;

    if (APP_ST25DV_CheckReady() != APP_ST25DV_OK)
    {
        return APP_ST25DV_NOT_READY;
    }

    pwd.MsbPasswd = msb;
    pwd.LsbPasswd = lsb;

    return APP_ST25DV_FromDrvStatus(ST25DV_PresentI2CPassword(&s_st25dv_obj, pwd));
}

ST25DV_Object_t *APP_ST25DV_GetObject(void)
{
    return &s_st25dv_obj;
}

uint32_t *payload_GetObject(void)
{
    if (BUS_MASTER_DEF)
    {
        return (uint32_t *)&master_payload;
    }

    return (uint32_t *)&slave_payload;
}

static void sync_config_to_flash_and_eeprom(uint8_t *buf, const uint8_t *eeprom_buf)
{
    uint16_t total_len;

    if ((buf == NULL) || (eeprom_buf == NULL))
    {
        return;
    }

    total_len = APP_ST25DV_GetTotalFrameLenByType(buf[FRAME_OFFSET_TYPE]);
    if (total_len == 0U)
    {
        return;
    }

    if (buf != eeprom_buf)
    {
        /* The newest valid frame came from flash. Mirror it into NFC EEPROM. */
        (void)APP_ST25DV_WriteBytes(0U, buf, total_len);
        UART_DBG_Printf_DMA("Load from flash\r\n");
        return;
    }

    /* The newest valid frame came from NFC EEPROM. Persist it into flash. */
    (void)storage_save(buf, total_len);
    UART_DBG_Printf_DMA("Load from eeprom\r\n");
}

APP_ST25DV_Status_t is_frame_valid(const uint8_t *frame_buf)
{
    uint16_t frame_len_without_crc;
    uint32_t stored_crc;
    uint32_t calc_crc;
    uint8_t type;

    if (frame_buf == NULL)
    {
        return APP_ST25DV_ERROR;
    }

    if (frame_buf[FRAME_OFFSET_SOF] != FRAME_SOF)
    {
        return APP_ST25DV_ERROR;
    }

    type = frame_buf[FRAME_OFFSET_TYPE];
    frame_len_without_crc = APP_ST25DV_GetFrameLenByType(type);
    if (frame_len_without_crc == 0U)
    {
        return APP_ST25DV_ERROR;
    }

    memcpy(&stored_crc, frame_buf + frame_len_without_crc, sizeof(stored_crc));
    calc_crc = storage_crc32(frame_buf, frame_len_without_crc);

    if (UART_DEBUG == 1)
    {
        UART_DBG_Printf_DMA("frame_crc:0x%08X calc:0x%08X\r\n", stored_crc, calc_crc);
    }

    return (stored_crc == calc_crc) ? APP_ST25DV_OK : APP_ST25DV_ERROR;
}

static APP_ST25DV_Status_t load_config_from_buffer(uint8_t *buf,
                                                   const uint8_t *eeprom_buf)
{
    payload_check_result_t ret;
    uint8_t type;

    if ((buf == NULL) || (eeprom_buf == NULL))
    {
        return APP_ST25DV_ERROR;
    }

    type = buf[FRAME_OFFSET_TYPE];

    switch (type)
    {
        case FRAME_MASTER_TYPE:
            memcpy(&master_payload,
                   buf + FRAME_OFFSET_PAYLOAD,
                   FRAME_MASTER_PAYLOAD_LEN);

            ret = check_master_payload(&master_payload);
            if (ret != PAYLOAD_CHECK_OK)
            {
                UART_DBG_Printf_DMA("master payload_check code: %d\r\n", ret);
                return APP_ST25DV_ERROR;
            }

            sync_config_to_flash_and_eeprom(buf, eeprom_buf);
            BUS_MASTER_DEF = 1;
            return APP_ST25DV_OK;

        case FRAME_SLAVE_TYPE:
            memcpy(&slave_payload,
                   buf + FRAME_OFFSET_PAYLOAD,
                   FRAME_SLAVE_PAYLOAD_LEN);

            ret = check_slave_payload(&slave_payload);
            if (ret != PAYLOAD_CHECK_OK)
            {
                UART_DBG_Printf_DMA("slave payload_check code: %d\r\n", ret);
                return APP_ST25DV_ERROR;
            }

            sync_config_to_flash_and_eeprom(buf, eeprom_buf);
            BUS_MASTER_DEF = 0;
            return APP_ST25DV_OK;

        default:
            UART_DBG_Printf_DMA("Unknown frame type: %d\r\n", type);
            return APP_ST25DV_ERROR;
    }
}

APP_ST25DV_Status_t APP_pragma_init(void)
{
    uint8_t flash_buffer[APP_ST25DV_FRAME_MAX_LEN] = {0};
    uint8_t eeprom_buffer[APP_ST25DV_FRAME_MAX_LEN] = {0};

    uint16_t flash_len = 0U;
    uint32_t flash_unix_time = 0U;
    uint32_t eeprom_unix_time = 0U;

    uint8_t flash_is_valid = 0U;

    HAL_StatusTypeDef flash_valid;
    APP_ST25DV_Status_t eeprom_valid;

    if (storage_init() != HAL_OK)
    {
        UART_DBG_Printf_DMA("Flash Init Fail\r\n");
    }
    else
    {
        UART_DBG_Printf_DMA("Flash Init\r\n");
    }

    flash_valid = storage_load_latest(flash_buffer, &flash_len);
    if ((flash_valid == HAL_OK) &&
        (flash_len <= APP_ST25DV_FRAME_MAX_LEN) &&
        (is_frame_valid(flash_buffer) == APP_ST25DV_OK))
    {
        flash_is_valid = 1U;
    }
    else
    {
        flash_is_valid = 0U;
    }

    UART_DBG_Printf_DMA("%s\r\n", (flash_is_valid != 0U) ? "flash valid" : "flash not valid");

    if (APP_ST25DV_Init() != APP_ST25DV_OK)
    {
        UART_DBG_Printf_DMA("NFC Init Fail\r\n");
    }
    else
    {
        UART_DBG_Printf_DMA("NFC Init\r\n");
    }

    (void)APP_ST25DV_ReadBytes(0U, eeprom_buffer, sizeof(eeprom_buffer));
    eeprom_valid = is_frame_valid(eeprom_buffer);
    UART_DBG_Printf_DMA("%s\r\n", (eeprom_valid == APP_ST25DV_OK) ? "eeprom valid" : "eeprom not valid");

    if ((eeprom_valid == APP_ST25DV_OK) && (flash_is_valid != 0U))
    {
        memcpy(&eeprom_unix_time, &eeprom_buffer[FRAME_OFFSET_TIME], sizeof(eeprom_unix_time));
        memcpy(&flash_unix_time,  &flash_buffer[FRAME_OFFSET_TIME],  sizeof(flash_unix_time));

        if (eeprom_unix_time > flash_unix_time)
        {
            if (load_config_from_buffer(eeprom_buffer, eeprom_buffer) != APP_ST25DV_OK)
            {
                return load_config_from_buffer(flash_buffer, eeprom_buffer);
            }
            return APP_ST25DV_OK;
        }

        return load_config_from_buffer(flash_buffer, eeprom_buffer);
    }

    if (eeprom_valid == APP_ST25DV_OK)
    {
        return load_config_from_buffer(eeprom_buffer, eeprom_buffer);
    }

    if (flash_is_valid != 0U)
    {
        return load_config_from_buffer(flash_buffer, eeprom_buffer);
    }

    return APP_ST25DV_ERROR;
}

static payload_check_result_t check_rf_param(const rf_param_t *rf)
{
    if (rf == NULL)
    {
        return PAYLOAD_CHECK_ERR_NULL;
    }

    if ((rf->pwr < RF_MIN_RAMP) || (rf->pwr > RF_MAX_RAMP))
    {
        return PAYLOAD_CHECK_ERR_RF_PWR;
    }

    if ((rf->freq < RF_FREQ_MIN_HZ) || (rf->freq > RF_FREQ_MAX_HZ))
    {
        return PAYLOAD_CHECK_ERR_RF_FREQ;
    }

    if ((rf->sf < SF_5) || (rf->sf > SF_12))
    {
        return PAYLOAD_CHECK_ERR_RF_SF;
    }

    if ((rf->bw != BW_62_5K) &&
        (rf->bw != BW_125K)  &&
        (rf->bw != BW_250K)  &&
        (rf->bw != BW_500K))
    {
        return PAYLOAD_CHECK_ERR_RF_BW;
    }

    if ((rf->cr != CODE_RATE_45) &&
        (rf->cr != CODE_RATE_46) &&
        (rf->cr != CODE_RATE_47) &&
        (rf->cr != CODE_RATE_48))
    {
        return PAYLOAD_CHECK_ERR_RF_CR;
    }

    return PAYLOAD_CHECK_OK;
}

static payload_check_result_t check_rs485_port_config(const rs485_port_config_t *port)
{
    if (port == NULL)
    {
        return PAYLOAD_CHECK_ERR_NULL;
    }

    if (port->baudrate_index > UART_BAUDRATE_INDEX_MAX)
    {
        return PAYLOAD_CHECK_ERR_RS485_BAUD;
    }

    if ((port->feature_flags & (uint8_t)(~RS485_FEATURE_VALID_MASK)) != 0U)
    {
        return PAYLOAD_CHECK_ERR_RS485_FEATURE;
    }

    return PAYLOAD_CHECK_OK;
}

payload_check_result_t check_master_payload(const master_payload_t *master)
{
    payload_check_result_t ret;

    if (master == NULL)
    {
        return PAYLOAD_CHECK_ERR_NULL;
    }

    ret = check_rf_param(&master->rf);
    if (ret != PAYLOAD_CHECK_OK)
    {
        return ret;
    }

    if ((master->vehicle_id < VEHICLE_ID_MIN) ||
        (master->vehicle_id > VEHICLE_ID_MAX))
    {
        return PAYLOAD_CHECK_ERR_VEHICLE_ID;
    }

    ret = check_rs485_port_config(&master->rs485_1);
    if (ret != PAYLOAD_CHECK_OK)
    {
        return ret;
    }

    ret = check_rs485_port_config(&master->rs485_2);
    if (ret != PAYLOAD_CHECK_OK)
    {
        return ret;
    }

    return PAYLOAD_CHECK_OK;
}

static payload_check_result_t check_seat_port_config(const seat_port_config_t *seat)
{
    if (seat == NULL)
    {
        return PAYLOAD_CHECK_ERR_NULL;
    }

    if ((seat->enable != SEAT_ENABLE_FALSE) && (seat->enable != SEAT_ENABLE_TRUE))
    {
        return PAYLOAD_CHECK_ERR_SEAT_ENABLE;
    }

    if (seat->enable == SEAT_ENABLE_TRUE)
    {
        if ((seat->seat_no < SEAT_NO_MIN) || (seat->seat_no > SEAT_NO_MAX))
        {
            return PAYLOAD_CHECK_ERR_SEAT_NO;
        }
    }

    return PAYLOAD_CHECK_OK;
}

payload_check_result_t check_slave_payload(const slave_payload_t *slave)
{
    payload_check_result_t ret;

    if (slave == NULL)
    {
        return PAYLOAD_CHECK_ERR_NULL;
    }

    ret = check_rf_param(&slave->rf);
    if (ret != PAYLOAD_CHECK_OK)
    {
        return ret;
    }

    if ((slave->vehicle_id < VEHICLE_ID_MIN) ||
        (slave->vehicle_id > VEHICLE_ID_MAX))
    {
        return PAYLOAD_CHECK_ERR_VEHICLE_ID;
    }

    ret = check_seat_port_config(&slave->seat[0]);
    if (ret != PAYLOAD_CHECK_OK)
    {
        return ret;
    }

    ret = check_seat_port_config(&slave->seat[1]);
    if (ret != PAYLOAD_CHECK_OK)
    {
        return ret;
    }

    if ((slave->seat[0].enable == SEAT_ENABLE_TRUE) &&
        (slave->seat[1].enable == SEAT_ENABLE_TRUE) &&
        (slave->seat[0].seat_no == slave->seat[1].seat_no))
    {
        return PAYLOAD_CHECK_ERR_SEAT_DUPLICATE;
    }

    if (((uint16_t)slave->seat_behavior.abnormal_order_mask & (uint16_t)(~ORDER_STATE_MASK_VALID)) != 0U)
    {
        return PAYLOAD_CHECK_ERR_ORDER_MASK;
    }

    if (((uint16_t)slave->seat_behavior.filter_order_mask & (uint16_t)(~ORDER_STATE_MASK_VALID)) != 0U)
    {
        return PAYLOAD_CHECK_ERR_ORDER_MASK;
    }

    if (slave->seat_behavior.filter_time_s > FILTER_TIME_S_MAX)
    {
        return PAYLOAD_CHECK_ERR_FILTER_TIME;
    }

    if ((slave->contend.slot_ms < SLAVE_SLOT_MS_MIN) ||
        (slave->contend.slot_ms > SLAVE_SLOT_MS_MAX))
    {
        return PAYLOAD_CHECK_ERR_SLOT_MS;
    }

    if ((slave->contend.contend_slot_count_n < SLAVE_CONTEND_N_MIN) ||
        (slave->contend.contend_slot_count_n > SLAVE_CONTEND_N_MAX))
    {
        return PAYLOAD_CHECK_ERR_CONTEND_N;
    }

    if ((slave->contend.idle_confirm_ms < SLAVE_IDLE_CONFIRM_MS_MIN) ||
        (slave->contend.idle_confirm_ms > SLAVE_IDLE_CONFIRM_MS_MAX))
    {
        return PAYLOAD_CHECK_ERR_IDLE_CONFIRM_MS;
    }

    if ((slave->contend.ack_timeout_ms < SLAVE_ACK_TIMEOUT_MS_MIN) ||
        (slave->contend.ack_timeout_ms > SLAVE_ACK_TIMEOUT_MS_MAX))
    {
        return PAYLOAD_CHECK_ERR_ACK_TIMEOUT_MS;
    }

    return PAYLOAD_CHECK_OK;
}

APP_ST25DV_Status_t APP_ST25DV_SaveCurrentPayload(uint32_t unix_time)
{
    uint8_t frame_buf[APP_ST25DV_FRAME_MAX_LEN] = {0};
    uint16_t frame_len = 0U;

    if (app_build_frame_from_current_payload(unix_time,
                                             frame_buf,
                                             sizeof(frame_buf),
                                             &frame_len) != APP_ST25DV_OK)
    {
        UART2_Printf_DMA("build frame failed\r\n");
        return APP_ST25DV_ERROR;
    }

    if (storage_save(frame_buf, frame_len) != HAL_OK)
    {
        UART2_Printf_DMA("storage_save failed\r\n");
        return APP_ST25DV_ERROR;
    }

    if (APP_ST25DV_WriteBytes(0U, frame_buf, frame_len) != APP_ST25DV_OK)
    {
        UART2_Printf_DMA("APP_ST25DV_WriteBytes failed\r\n");
        return APP_ST25DV_ERROR;
    }

    return APP_ST25DV_OK;
}

void nfc_task_in_tim(void)
{
    uint8_t it_status = 0U;
    ST25DV_Object_t *pObj = APP_ST25DV_GetObject();
    uint8_t eeprom_buffer[APP_ST25DV_FRAME_MAX_LEN] = {0};
    APP_ST25DV_Status_t eeprom_valid;

    if (pObj == NULL)
    {
        return;
    }

    if (ST25DV_ReadITSTStatus_Dyn(pObj, &it_status) != 0)
    {
        return;
    }

    if (it_status == 0U)
    {
        return;
    }

    if (APP_ST25DV_ReadBytes(0U, eeprom_buffer, sizeof(eeprom_buffer)) != APP_ST25DV_OK)
    {
        return;
    }

    eeprom_valid = is_frame_valid(eeprom_buffer);
    if (eeprom_valid == APP_ST25DV_OK)
    {
        NVIC_SystemReset();
    }
}

void APP_ST25DV_RunTest(void)
{
    uint8_t id = 0;
    uint8_t rev = 0;
    ST25DV_UID uid;
    uint8_t tx_buf[16];
    uint8_t rx_buf[16];

    UART_DBG_Printf_DMA("\r\n===== ST25DV TEST START =====\r\n");

    /* 1. 初始化 */
    if (APP_ST25DV_Init() != APP_ST25DV_OK)
    {
        UART_DBG_Printf_DMA("Init FAILED!\r\n");
        return;
    }
    UART_DBG_Printf_DMA("Init OK\r\n");

    /* 2. 检测设备是否在线 */
    if (APP_ST25DV_IsDeviceReady(5) != APP_ST25DV_OK)
    {
        UART_DBG_Printf_DMA("Device NOT READY!\r\n");
        return;
    }
    UART_DBG_Printf_DMA("Device Ready\r\n");

    /* 3. 读取 ID */
    if (APP_ST25DV_ReadId(&id) == APP_ST25DV_OK)
    {
        UART_DBG_Printf_DMA("IC Ref: 0x%02X\r\n", id);
    }
    else
    {
        UART_DBG_Printf_DMA("Read ID FAILED\r\n");
    }

    /* 4. 读取 Revision */
    if (APP_ST25DV_ReadIcRev(&rev) == APP_ST25DV_OK)
    {
        UART_DBG_Printf_DMA("IC Rev: 0x%02X\r\n", rev);
    }
    else
    {
        UART_DBG_Printf_DMA("Read Rev FAILED\r\n");
    }

    /* 5. 读取 UID */
    if (APP_ST25DV_ReadUid(&uid) == APP_ST25DV_OK)
    {
        UART_DBG_Printf_DMA("UID: %08lX%08lX\r\n", uid.MsbUid, uid.LsbUid);
    }
    else
    {
        UART_DBG_Printf_DMA("Read UID FAILED\r\n");
    }

    /* 6. 准备测试数据 */
    for (int i = 0; i < sizeof(tx_buf); i++)
    {
        tx_buf[i] = i;
        rx_buf[i] = 0;
    }

    UART_DBG_Printf_DMA("Write Data...\r\n");

    /* 7. 写 EEPROM（地址 0x0000） */
    if (APP_ST25DV_WriteBytes(0x0000, tx_buf, sizeof(tx_buf)) != APP_ST25DV_OK)
    {
        UART_DBG_Printf_DMA("Write FAILED\r\n");
        return;
    }

    HAL_Delay(10);  // 等 EEPROM 稳定

    /* 8. 读回数据 */
    if (APP_ST25DV_ReadBytes(0x0000, rx_buf, sizeof(rx_buf)) != APP_ST25DV_OK)
    {
        UART_DBG_Printf_DMA("Read FAILED\r\n");
        return;
    }

    /* 9. 打印对比 */
    UART_DBG_Printf_DMA("Read Back:\r\n");

    for (int i = 0; i < sizeof(rx_buf); i++)
    {
        UART_DBG_Printf_DMA("%02X ", rx_buf[i]);
    }
    UART_DBG_Printf_DMA("\r\n");

    /* 10. 校验 */
    if (memcmp(tx_buf, rx_buf, sizeof(tx_buf)) == 0)
    {
        UART_DBG_Printf_DMA("DATA VERIFY OK\r\n");
    }
    else
    {
        UART_DBG_Printf_DMA("DATA VERIFY FAILED\r\n");
    }

    UART_DBG_Printf_DMA("===== ST25DV TEST END =====\r\n\r\n");
}
