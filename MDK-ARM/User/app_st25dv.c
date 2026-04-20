#include "app_st25dv.h"
#include "custom_bus.h"
#include <string.h>
#include "rs485bsp.h"
#include "flash_storage.h"

slave_payload_t slave_payload;
master_payload_t master_payload;

static uint16_t APP_ST25DV_GetFrameLenByType(uint8_t type)
{
    return (type == FRAME_MASTER_TYPE) ? FRAME_MASTER_LEN : FRAME_SLAVE_LEN;
}

static uint16_t APP_ST25DV_GetPayloadLenByType(uint8_t type)
{
    return (type == FRAME_MASTER_TYPE) ? FRAME_MASTER_PAYLOAD_LEN : FRAME_SLAVE_PAYLOAD_LEN;
}

static void APP_ST25DV_SetReservedValid(uint8_t *buf)
{
    if (buf != NULL)
    {
        buf[RESERVED_OFFSET_IN_FRAME] |= RESERVED_VALID_MASK;
    }
}

static uint8_t APP_ST25DV_IsReservedValid(const uint8_t *buf)
{
    if (buf == NULL)
    {
        return 0U;
    }

    return ((buf[RESERVED_OFFSET_IN_FRAME] & RESERVED_VALID_MASK) != 0U) ? 1U : 0U;
}

static void APP_ST25DV_CopyReservedFromFrame(uint8_t *dst_buf, const uint8_t *src_buf)
{
    if ((dst_buf == NULL) || (src_buf == NULL))
    {
        return;
    }

    memcpy(dst_buf + RESERVED_OFFSET_IN_FRAME,
           src_buf + RESERVED_OFFSET_IN_FRAME,
           RESERVED_LEN);
}

static void APP_ST25DV_UpdateFrameCrc(uint8_t *buf)
{
    uint16_t frame_len;
    uint32_t crc;

    if (buf == NULL)
    {
        return;
    }

    frame_len = APP_ST25DV_GetFrameLenByType(buf[FRAME_OFFSET_TYPE]);
    crc = storage_crc32(buf, frame_len);
    memcpy(buf + frame_len, &crc, sizeof(crc));
}






static uint16_t app_get_frame_len_by_type(uint8_t type)
{
    switch (type)
    {
        case FRAME_MASTER_TYPE:
            return (FRAME_MASTER_LEN + 4U); /* + CRC32 */
        case FRAME_SLAVE_TYPE:
            return (FRAME_SLAVE_LEN + 4U);  /* + CRC32 */
        default:
            return 0U;
    }
}

static uint16_t app_get_payload_len_by_type(uint8_t type)
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

static void app_set_reserved_valid(uint8_t type, uint8_t *payload_buf)
{
    if (payload_buf == NULL)
    {
        return;
    }

    if (type == FRAME_MASTER_TYPE)
    {
        master_payload_t *p = (master_payload_t *)payload_buf;
        p->reserved[0] |= RESERVED_VALID_MASK;
    }
    else if (type == FRAME_SLAVE_TYPE)
    {
        slave_payload_t *p = (slave_payload_t *)payload_buf;
        p->reserved[0] |= RESERVED_VALID_MASK;
    }
}

static APP_ST25DV_Status_t app_build_frame_from_current_payload(uint32_t unix_time,
                                                                uint8_t *frame_buf,
                                                                uint16_t frame_buf_size,
                                                                uint16_t *out_frame_len)
{
    uint8_t type;
    uint16_t payload_len;
    uint16_t frame_len;
    uint32_t crc;

    if ((frame_buf == NULL) || (out_frame_len == NULL))
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    if (BUS_MASTER_DEF)
    {
        type = FRAME_MASTER_TYPE;
        payload_len = FRAME_MASTER_PAYLOAD_LEN;
        frame_len = FRAME_MASTER_LEN + 4U;
    }
    else
    {
        type = FRAME_SLAVE_TYPE;
        payload_len = FRAME_SLAVE_PAYLOAD_LEN;
        frame_len = FRAME_SLAVE_LEN + 4U;
    }

    if (frame_buf_size < frame_len)
    {
        return APP_ST25DV_INVALID_PARAM;
    }

    memset(frame_buf, 0, frame_buf_size);

    /* SOF */
    frame_buf[FRAME_OFFSET_SOF] = FRAME_SOF;

    /* TIME */
    memcpy(&frame_buf[FRAME_OFFSET_TIME], &unix_time, sizeof(unix_time));

    /* TYPE */
    frame_buf[FRAME_OFFSET_TYPE] = type;

    /* PAYLOAD */
    if (type == FRAME_MASTER_TYPE)
    {
        memcpy(frame_buf + FRAME_OFFSET_PAYLOAD,
               &master_payload,
               payload_len);
    }
    else
    {
        memcpy(frame_buf + FRAME_OFFSET_PAYLOAD,
               &slave_payload,
               payload_len);
    }

    /* reserved[0] bit0 置有效 */
    app_set_reserved_valid(type, frame_buf + FRAME_OFFSET_PAYLOAD);

    /* CRC32: 对 [SOF + TIME + TYPE + PAYLOAD] 计算 */
    crc = storage_crc32(frame_buf, (uint16_t)(frame_len - 4U));
    memcpy(frame_buf + (frame_len - 4U), &crc, sizeof(crc));

    *out_frame_len = frame_len;
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
	if(BUS_MASTER_DEF)return (uint32_t *)&master_payload;
	else return (uint32_t *)&slave_payload;
}


static void save_to_flash_if_needed(uint8_t *buf, const uint8_t *eeprom_buf)
{
    uint16_t frame_len;

    if ((buf == NULL) || (eeprom_buf == NULL))
    {
        return;
    }

    if (buf != eeprom_buf)
    {
				APP_ST25DV_WriteBytes(0,buf, FRAME_TOTAL_LEN(frame_len));
        UART_DBG_Printf_DMA("Load from flash\r\n");
        return;
    }

    frame_len = APP_ST25DV_GetFrameLenByType(buf[FRAME_OFFSET_TYPE]);
    APP_ST25DV_SetReservedValid(buf);
    APP_ST25DV_UpdateFrameCrc(buf);

    storage_save(buf, FRAME_TOTAL_LEN(frame_len));
		APP_ST25DV_WriteBytes(0,buf, FRAME_TOTAL_LEN(frame_len));
    UART_DBG_Printf_DMA("Load from eeprom\r\n");
}

APP_ST25DV_Status_t is_frame_valid(const uint8_t *frame_buf)
{
    uint16_t len;
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
    switch (type)
    {
        case FRAME_MASTER_TYPE:
            len = FRAME_MASTER_LEN;
            break;

        case FRAME_SLAVE_TYPE:
            len = FRAME_SLAVE_LEN;
            break;

        default:
            return APP_ST25DV_ERROR;
    }

    memcpy(&stored_crc, frame_buf + len, sizeof(stored_crc));
    calc_crc = storage_crc32(frame_buf, len);

    if (UART_DEBUG == 1)
    {
        UART_DBG_Printf_DMA("eeprom_crc:0x%08X crc:0x%08X\r\n", stored_crc, calc_crc);
    }

    return (stored_crc == calc_crc) ? APP_ST25DV_OK : APP_ST25DV_ERROR;
}

static APP_ST25DV_Status_t load_config_from_buffer(uint8_t *buf,
                                                   const uint8_t *eeprom_buf,
                                                   const uint8_t *flash_buf,
                                                   uint8_t flash_is_valid)
{
    payload_check_result_t ret;
    uint8_t type;

    if ((buf == NULL) || (eeprom_buf == NULL))
    {
        return APP_ST25DV_ERROR;
    }

    type = buf[FRAME_OFFSET_TYPE];

    if ((buf == eeprom_buf) && (APP_ST25DV_IsReservedValid(buf) == 0U))
    {
			if(flash_is_valid != 0U)
        APP_ST25DV_CopyReservedFromFrame(buf, flash_buf);
			else
				return APP_ST25DV_ERROR;
    }

    switch (type)
    {
        case FRAME_MASTER_TYPE:
            memcpy(&master_payload,
                   buf + FRAME_OFFSET_PAYLOAD,
                   FRAME_MASTER_PAYLOAD_LEN);

            ret = check_master_payload(&master_payload);
            if (ret != PAYLOAD_CHECK_OK)
            {
                UART_DBG_Printf_DMA("payload_check code: %d\r\n", ret);
                return APP_ST25DV_ERROR;
            }

            save_to_flash_if_needed(buf, eeprom_buf);
            BUS_MASTER_DEF = 1;
            return APP_ST25DV_OK;

        case FRAME_SLAVE_TYPE:
            memcpy(&slave_payload,
                   buf + FRAME_OFFSET_PAYLOAD,
                   FRAME_SLAVE_PAYLOAD_LEN);

            ret = check_slave_payload(&slave_payload);
            if (ret != PAYLOAD_CHECK_OK)
            {
                UART_DBG_Printf_DMA("payload_check code: %d\r\n", ret);
                return APP_ST25DV_ERROR;
            }

            save_to_flash_if_needed(buf, eeprom_buf);
            BUS_MASTER_DEF = 0;
            return APP_ST25DV_OK;

        default:
            UART_DBG_Printf_DMA("Unknown frame type: %d\r\n", type);
            return APP_ST25DV_ERROR;
    }
}

APP_ST25DV_Status_t APP_pragma_init(void)
{
    uint8_t flash_buffer[64] = {0};
    uint8_t eeprom_buffer[64] = {0};

    uint16_t flash_len = 0;
    uint32_t flash_unix_time = 0;
    uint32_t eeprom_unix_time = 0;

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
    flash_is_valid = (flash_valid == HAL_OK) ? 1U : 0U;
    UART_DBG_Printf_DMA("%s\r\n", (flash_is_valid != 0U) ? "flash valid" : "flash not valid");

    if (APP_ST25DV_Init() != APP_ST25DV_OK)
    {
        UART_DBG_Printf_DMA("NFC Init Fail\r\n");
    }
    else
    {
        UART_DBG_Printf_DMA("NFC Init\r\n");
    }

    APP_ST25DV_ReadBytes(0, eeprom_buffer, sizeof(eeprom_buffer));
    eeprom_valid = is_frame_valid(eeprom_buffer);
    UART_DBG_Printf_DMA("%s\r\n", (eeprom_valid == APP_ST25DV_OK) ? "eeprom valid" : "eeprom not valid");

    if ((eeprom_valid == APP_ST25DV_OK) && (flash_is_valid != 0U))
    {
        memcpy(&eeprom_unix_time, &eeprom_buffer[FRAME_OFFSET_TIME], sizeof(eeprom_unix_time));
        memcpy(&flash_unix_time,  &flash_buffer[FRAME_OFFSET_TIME],  sizeof(flash_unix_time));

        if (eeprom_unix_time > flash_unix_time)
        {
            if (load_config_from_buffer(eeprom_buffer, eeprom_buffer, flash_buffer, flash_is_valid) != APP_ST25DV_OK)
            {
                return load_config_from_buffer(flash_buffer, eeprom_buffer, flash_buffer, flash_is_valid);
            }
            return APP_ST25DV_OK;
        }

        return load_config_from_buffer(flash_buffer, eeprom_buffer, flash_buffer, flash_is_valid);
    }

    if (eeprom_valid == APP_ST25DV_OK)
    {
        return load_config_from_buffer(eeprom_buffer, eeprom_buffer, flash_buffer, flash_is_valid);
    }

    if (flash_is_valid != 0U)
    {
        return load_config_from_buffer(flash_buffer, eeprom_buffer, flash_buffer, flash_is_valid);
    }

    return APP_ST25DV_ERROR;
}

static payload_check_result_t check_rf_param(const rf_param_t *rf)
{
    if (rf == NULL)
    {
        return PAYLOAD_CHECK_ERR_NULL;
    }

    /* 功率范围 */
    if ((rf->pwr < RF_MIN_RAMP) || (rf->pwr > RF_MAX_RAMP))
    {
        return PAYLOAD_CHECK_ERR_RF_PWR;
    }

    /* 频率范围：405MHz ~ 1080MHz */
    if ((rf->freq < RF_FREQ_MIN_HZ) || (rf->freq > RF_FREQ_MAX_HZ))
    {
        return PAYLOAD_CHECK_ERR_RF_FREQ;
    }

    /* SF */
    if ((rf->sf < SF_5) || (rf->sf > SF_12))
    {
        return PAYLOAD_CHECK_ERR_RF_SF;
    }

    /* BW */
    if ((rf->bw != BW_62_5K) &&
        (rf->bw != BW_125K)  &&
        (rf->bw != BW_250K)  &&
        (rf->bw != BW_500K))
    {
        return PAYLOAD_CHECK_ERR_RF_BW;
    }

    /* CR */
    if ((rf->cr != CODE_RATE_45) &&
        (rf->cr != CODE_RATE_46) &&
        (rf->cr != CODE_RATE_47) &&
        (rf->cr != CODE_RATE_48))
    {
        return PAYLOAD_CHECK_ERR_RF_CR;
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

    /* 先校验 RF */
    ret = check_rf_param(&master->rf_param);
    if (ret != PAYLOAD_CHECK_OK)
    {
        return ret;
    }

    /* vehicle_id */
    if ((master->vehicle_id < VEHICLE_ID_MIN) ||
        (master->vehicle_id > VEHICLE_ID_MAX))
    {
        return PAYLOAD_CHECK_ERR_VEHICLE_ID;
    }

    /* 轮询从机范围 */
    if (master->poll_start_slave >= BUS_MASTER_MAX_SLAVES)
    {
        return PAYLOAD_CHECK_ERR_POLL_START_SEAT;
    }

    if (master->poll_end_slave >= BUS_MASTER_MAX_SLAVES)
    {
        return PAYLOAD_CHECK_ERR_POLL_END_SEAT;
    }

    if (master->poll_start_slave > master->poll_end_slave)
    {
        return PAYLOAD_CHECK_ERR_POLL_RANGE;
    }

    /* max_retry */
    if ((master->max_retry < MASTER_MAX_RETRY_MIN) ||
        (master->max_retry > MASTER_MAX_RETRY_MAX))
    {
        return PAYLOAD_CHECK_ERR_MAX_RETRY;
    }

    /* 超时 */
    if ((master->reply_timeout_ms < MASTER_REPLY_TIMEOUT_MS_MIN) ||
        (master->reply_timeout_ms > MASTER_REPLY_TIMEOUT_MS_MAX))
    {
        return PAYLOAD_CHECK_ERR_REPLY_TIMEOUT;
    }

    if ((master->inter_query_gap_ms < MASTER_INTER_QUERY_GAP_MS_MIN) ||
        (master->inter_query_gap_ms > MASTER_INTER_QUERY_GAP_MS_MAX))
    {
        return PAYLOAD_CHECK_ERR_INTER_QUERY_GAP;
    }

    /* 竞争窗口 */
    if ((master->contend_M < MASTER_CONTEND_M_MIN) ||
        (master->contend_M > MASTER_CONTEND_M_MAX))
    {
        return PAYLOAD_CHECK_ERR_CONTEND_M;
    }

    /* 时间参数 */
    if ((master->cad_idle_ms < MASTER_CAD_IDLE_MS_MIN) ||
        (master->cad_idle_ms > MASTER_CAD_IDLE_MS_MAX))
    {
        return PAYLOAD_CHECK_ERR_CAD_IDLE_MS;
    }

    if ((master->round_sleep_ms < MASTER_ROUND_SLEEP_MS_MIN) ||
        (master->round_sleep_ms > MASTER_ROUND_SLEEP_MS_MAX))
    {
        return PAYLOAD_CHECK_ERR_ROUND_SLEEP_MS;
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

    /* 先校验 RF */
    ret = check_rf_param(&slave->rf_param);
    if (ret != PAYLOAD_CHECK_OK)
    {
        return ret;
    }

    /* vehicle_id */
    if ((slave->vehicle_id < VEHICLE_ID_MIN) ||
        (slave->vehicle_id > VEHICLE_ID_MAX))
    {
        return PAYLOAD_CHECK_ERR_VEHICLE_ID;
    }

    /* seat_id / slave_id 范围 */
    if (slave->seat_id >= BUS_MASTER_MAX_SLAVES)
    {
        return PAYLOAD_CHECK_ERR_SEAT_ID;
    }

    return PAYLOAD_CHECK_OK;
}
APP_ST25DV_Status_t APP_ST25DV_SaveCurrentPayload(uint32_t unix_time)
{
    uint8_t frame_buf[64] = {0};
    uint16_t frame_len = 0U;

    if (app_build_frame_from_current_payload(unix_time,
                                             frame_buf,
                                             sizeof(frame_buf),
                                             &frame_len) != APP_ST25DV_OK)
    {
        UART2_Printf_DMA("build frame failed\r\n");
        return APP_ST25DV_ERROR;
    }

    /* 先写 flash */
    if (storage_save(frame_buf, frame_len) != HAL_OK)
    {
        UART2_Printf_DMA("storage_save failed\r\n");
        return APP_ST25DV_ERROR;
    }

    /* 再写 EEPROM */
    if (APP_ST25DV_WriteBytes(0U, frame_buf, frame_len) != APP_ST25DV_OK)
    {
        UART2_Printf_DMA("APP_ST25DV_WriteBytes failed\r\n");
        return APP_ST25DV_ERROR;
    }

    return APP_ST25DV_OK;
}

void nfc_task_in_tim(void)
{
    uint8_t it_status = 0;
    ST25DV_Object_t *pObj = APP_ST25DV_GetObject();

    if (pObj == NULL)
    {
        return;
    }

    if (ST25DV_ReadITSTStatus_Dyn(pObj, &it_status) != 0)
    {
        return;
    }

    if (it_status == 0)
    {
        return;
    }
		uint8_t eeprom_buffer[64];
		APP_ST25DV_ReadBytes(0,eeprom_buffer,sizeof(eeprom_buffer));
		APP_ST25DV_Status_t eeprom_valid;
		eeprom_valid = is_frame_valid(eeprom_buffer);
		if(eeprom_valid == APP_ST25DV_OK)
		{
			NVIC_SystemReset();
		}
}
	

/* 测试函数 */
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
