#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Frame format is kept compatible with the previous frame_builder:
 * [SOF][TIME][TYPE][PAYLOAD][CRC32]
 *
 * SOF   : 1 byte, fixed 0xAA
 * TIME  : 4 bytes, unix timestamp in seconds, little-endian
 * TYPE  : 1 byte, master or slave
 * PAYLOAD:
 *   master = rf(8) + reserved(16) + master business fields
 *   slave  = rf(8) + reserved(16) + slave business fields
 * CRC32 : 4 bytes, calculated over SOF + TIME + TYPE + PAYLOAD
 *         using STM32-style CRC32 (word-fed, little-endian, 0xFF pad)
 *
 * Notes:
 * - Former reserved-byte business meanings are moved into explicit structs.
 * - The 16-byte reserved segment is still kept for future expansion.
 * - config_version/config_flags/max_seat_no/slave_id are not included.
 * - master_ack_gap_ms is runtime behavior, not a configurable parameter.
 * - RS485 functions are independent feature enables, not exclusive modes.
 */

#define ftp2phone

/* ================= Frame constants ================= */

#define FRAME_SOF                         0xAAu
#define FRAME_MASTER_TYPE                 0x01u
#define FRAME_SLAVE_TYPE                  0x02u
#define RESERVED_LEN                      16u

/* ================= RF default parameters ================= */

#define DEFAULT_RF_PWR                    22u
#define DEFAULT_RF_FREQ                   487914814UL
#define DEFAULT_RF_SF                     7u
#define DEFAULT_RF_BW                     8u
#define DEFAULT_RF_CR                     2u

/* ================= Vehicle parameters ================= */

#define DEFAULT_VEHICLE_ID                18u

/* ================= RS485 feature flags, master only ================= */

#define RS485_FEATURE_NONE                0x00u
#define RS485_FEATURE_JSON_OUT            0x01u  /* Output frame to upper computer. */
#define RS485_FEATURE_FORWARD_TO_OTHER    0x02u  /* Forward received data to the other 485 port. */
#define RS485_FEATURE_LOG_OUT             0x04u  /* Optional diagnostic output. */

#define UART_BAUD_115200_INDEX            11u

/* ================= Seat behavior parameters, slave only ================= */

#define ORDER_MASK(order_state)           ((uint16_t)(1u << ((order_state) - 1u)))

#define DEFAULT_ABNORMAL_ORDER_MASK       0x00

#define DEFAULT_FILTER_ORDER_MASK         0xFFF
#define DEFAULT_FILTER_TIME_S             32u

/* Buzzer duration: 0 = no alarm, 255 = alarm until next confirmed state. */
#define DEFAULT_ALARM_DURATION_S          2u
#define DEFAULT_ALARM_DELAY_S             5u

/* ================= Slave LoRa contention parameters ================= */

#define DEFAULT_SLOT_MS                   5u
#define DEFAULT_CONTEND_SLOT_COUNT_N      10u
#define DEFAULT_IDLE_CONFIRM_MS           10u
#define DEFAULT_ACK_TIMEOUT_MS            35u

/* 255 means retry forever until the event is acknowledged. */
#define DEFAULT_SLAVE_MAX_RETRY           10u

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
    uint8_t seat_no;    /* Seat number configured by NFC. 0 is invalid/unused. */
} seat_port_config_t;

typedef struct
{
    uint16_t abnormal_order_mask;  /* bit0 -> order_state 1, bit11 -> order_state 12. */
    uint16_t filter_order_mask;    /* bit0 -> order_state 1, bit11 -> order_state 12. */
    uint8_t  filter_time_s;        /* 0~15 s recommended. */
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

    seat_port_config_t seat[2];
    seat_behavior_config_t seat_behavior;
    slave_contend_config_t contend;
} slave_payload_t;

#pragma pack(pop)

/* ================= UART baudrate table ================= */

static const uint32_t uart_baudrate_list[] = {
    300,
    600,
    1200,
    2400,
    4800,
    9600,
    14400,
    19200,
    28800,
    38400,
    57600,
    115200,
    230400,
    460800,
    921600
};

#define UART_BAUDRATE_COUNT (sizeof(uart_baudrate_list) / sizeof(uart_baudrate_list[0]))

static void write_u32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static uint32_t current_unix_time_u32(void)
{
    time_t now = time(NULL);
    if (now < 0)
    {
        return 0U;
    }
    return (uint32_t)now;
}

/*
 * STM32 CRC peripheral style:
 * - Polynomial: 0x04C11DB7
 * - Init: 0xFFFFFFFF
 * - No final xor
 * - Feed as 32-bit words
 * - Each word is packed from byte stream in little-endian order
 * - Tail is padded with 0xFF to 4-byte boundary
 */
static uint32_t stm32_crc32_accumulate_word(uint32_t crc, uint32_t data_word)
{
    crc ^= data_word;

    for (int i = 0; i < 32; i++)
    {
        if (crc & 0x80000000UL)
        {
            crc = (crc << 1) ^ 0x04C11DB7UL;
        }
        else
        {
            crc <<= 1;
        }
    }

    return crc;
}

static uint32_t storage_crc32_pc(const uint8_t *data, uint16_t len)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint32_t word;
    uint16_t i = 0U;

    while (i < len)
    {
        word = 0xFFFFFFFFUL;

        ((uint8_t *)&word)[0] = data[i++];
        if (i < len) ((uint8_t *)&word)[1] = data[i++];
        if (i < len) ((uint8_t *)&word)[2] = data[i++];
        if (i < len) ((uint8_t *)&word)[3] = data[i++];

        crc = stm32_crc32_accumulate_word(crc, word);
    }

    return crc;
}

static int save_file(const char *filename, const uint8_t *data, size_t len)
{
    FILE *fp = fopen(filename, "wb");
    if (fp == NULL)
    {
        perror("fopen");
        return -1;
    }

    if (fwrite(data, 1, len, fp) != len)
    {
        perror("fwrite");
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static void print_hex(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        printf("%02X", data[i]);
        if (i + 1 < len)
        {
            printf(" ");
        }
    }
    printf("\n");
}

static int build_frame(uint8_t type,
                       uint32_t unix_time,
                       const void *payload,
                       uint16_t payload_len,
                       uint8_t *out_buf,
                       uint16_t out_buf_size,
                       uint16_t *out_len)
{
    uint16_t frame_len_without_crc;
    uint16_t total_len;
    uint32_t crc;

    if (payload == NULL || out_buf == NULL || out_len == NULL)
    {
        return -1;
    }

    frame_len_without_crc = (uint16_t)(1 + 4 + 1 + payload_len);
    total_len = (uint16_t)(frame_len_without_crc + 4);

    if (out_buf_size < total_len)
    {
        return -2;
    }

    out_buf[0] = FRAME_SOF;
    write_u32_le(&out_buf[1], unix_time);
    out_buf[5] = type;
    memcpy(&out_buf[6], payload, payload_len);

    crc = storage_crc32_pc(out_buf, frame_len_without_crc);
    write_u32_le(&out_buf[6 + payload_len], crc);

    *out_len = total_len;
    return 0;
}

static void fill_rf_default(rf_param_t *rf)
{
    rf->pwr  = DEFAULT_RF_PWR;
    rf->freq = DEFAULT_RF_FREQ;
    rf->sf   = DEFAULT_RF_SF;
    rf->bw   = DEFAULT_RF_BW;
    rf->cr   = DEFAULT_RF_CR;
}

static void print_baudrate(uint8_t baudrate_index)
{
    printf("%u", (unsigned)baudrate_index);
    if (baudrate_index < UART_BAUDRATE_COUNT)
    {
        printf(" (%u)", (unsigned)uart_baudrate_list[baudrate_index]);
    }
}

static void print_master_config(const master_payload_t *m)
{
    printf("master.vehicle_id             = %u\n", (unsigned)m->vehicle_id);

    printf("master.rs485_1.baud_index     = ");
    print_baudrate(m->rs485_1.baudrate_index);
    printf("\n");
    printf("master.rs485_1.feature_flags  = 0x%02X\n", (unsigned)m->rs485_1.feature_flags);

    printf("master.rs485_2.baud_index     = ");
    print_baudrate(m->rs485_2.baudrate_index);
    printf("\n");
    printf("master.rs485_2.feature_flags  = 0x%02X\n", (unsigned)m->rs485_2.feature_flags);
}

static void print_slave_config(const slave_payload_t *s)
{
    printf("slave.vehicle_id              = %u\n", (unsigned)s->vehicle_id);
    printf("slave.seat[0].enable          = %u\n", (unsigned)s->seat[0].enable);
    printf("slave.seat[0].seat_no         = %u\n", (unsigned)s->seat[0].seat_no);
    printf("slave.seat[1].enable          = %u\n", (unsigned)s->seat[1].enable);
    printf("slave.seat[1].seat_no         = %u\n", (unsigned)s->seat[1].seat_no);
    printf("slave.abnormal_order_mask     = 0x%04X\n", (unsigned)s->seat_behavior.abnormal_order_mask);
    printf("slave.filter_order_mask       = 0x%04X\n", (unsigned)s->seat_behavior.filter_order_mask);
    printf("slave.filter_time_s           = %u\n", (unsigned)s->seat_behavior.filter_time_s);
    printf("slave.alarm_delay_s           = %u\n", (unsigned)s->seat_behavior.alarm_delay_s);
    printf("slave.alarm_duration_s        = %u\n", (unsigned)s->seat_behavior.alarm_duration_s);
    printf("slave.slot_ms                 = %u\n", (unsigned)s->contend.slot_ms);
    printf("slave.contend_slot_count_n    = %u\n", (unsigned)s->contend.contend_slot_count_n);
    printf("slave.idle_confirm_ms         = %u\n", (unsigned)s->contend.idle_confirm_ms);
    printf("slave.ack_timeout_ms          = %u\n", (unsigned)s->contend.ack_timeout_ms);
    printf("slave.max_retry               = %u\n", (unsigned)s->contend.max_retry);
}


int upload_by_curl(const char *local_file, const char *remote_file)
{
    char cmd[2048];

    snprintf(cmd, sizeof(cmd),
             "curl.exe -sS --fail --disable-epsv "
             "--connect-timeout 10 --max-time 60 "
             "--ftp-create-dirs "
             "--user \"15191722090:198282abc\" "
             "-T \"%s\" "
             "\"ftp://10.196.180.158:2121/360file/%s\"",
             local_file,
             remote_file);

    int ret = system(cmd);
    if (ret != 0)
    {
        printf("curl upload failed, ret=%d\n", ret);
        return 1;
    }

    return 0;
}

int main(void)
{
    uint32_t now = current_unix_time_u32();

    master_payload_t master;
    slave_payload_t slave;

    memset(&master, 0, sizeof(master));
    memset(&slave, 0, sizeof(slave));

    fill_rf_default(&master.rf);
    fill_rf_default(&slave.rf);

    /* ===== Manual master configuration =====
     * Example plan:
     * - RS485_1 connects to upper computer and outputs JSON.
     * - RS485_2 connects to sensor bus and forwards received data to the other port.
     *
     * RS485 feature flags are independent enable bits. Multiple functions can be
     * enabled at the same time when the firmware supports the combination.
     */
    master.vehicle_id = DEFAULT_VEHICLE_ID;
    master.rs485_1.baudrate_index = UART_BAUD_115200_INDEX;
    master.rs485_1.feature_flags = (uint8_t)(RS485_FEATURE_JSON_OUT | RS485_FEATURE_FORWARD_TO_OTHER | RS485_FEATURE_LOG_OUT);
    master.rs485_2.baudrate_index = UART_BAUD_115200_INDEX;
    master.rs485_2.feature_flags = (uint8_t)(RS485_FEATURE_LOG_OUT | RS485_FEATURE_FORWARD_TO_OTHER);

    /* ===== Manual slave configuration =====
     * Seat enable rule:
     * - enable = 1: alarm and report are both enabled for this seat.
     * - enable = 0: neither alarm nor report is performed for this seat.
     * Runtime report only needs order_state; raw seat/belt status is not reported.
     */
    slave.vehicle_id = DEFAULT_VEHICLE_ID;

    slave.seat[0].enable = 1u;
    slave.seat[0].seat_no = 1u;
    slave.seat[1].enable = 1u;
    slave.seat[1].seat_no = 2u;

    slave.seat_behavior.abnormal_order_mask = DEFAULT_ABNORMAL_ORDER_MASK;
    slave.seat_behavior.filter_order_mask = DEFAULT_FILTER_ORDER_MASK;
    slave.seat_behavior.filter_time_s = DEFAULT_FILTER_TIME_S;
    slave.seat_behavior.alarm_delay_s = DEFAULT_ALARM_DELAY_S;
    slave.seat_behavior.alarm_duration_s = DEFAULT_ALARM_DURATION_S;

    slave.contend.slot_ms = DEFAULT_SLOT_MS;
    slave.contend.contend_slot_count_n = DEFAULT_CONTEND_SLOT_COUNT_N;
    slave.contend.idle_confirm_ms = DEFAULT_IDLE_CONFIRM_MS;
    slave.contend.ack_timeout_ms = DEFAULT_ACK_TIMEOUT_MS;
    slave.contend.max_retry = DEFAULT_SLAVE_MAX_RETRY;

    uint8_t master_buf[128];
    uint8_t slave_buf[128];
    uint16_t master_len = 0;
    uint16_t slave_len = 0;

    int ret;

    ret = build_frame(FRAME_MASTER_TYPE,
                      now,
                      &master,
                      (uint16_t)sizeof(master),
                      master_buf,
                      sizeof(master_buf),
                      &master_len);
    if (ret != 0)
    {
        printf("build master frame failed: %d\n", ret);
        return 1;
    }

    ret = build_frame(FRAME_SLAVE_TYPE,
                      now,
                      &slave,
                      (uint16_t)sizeof(slave),
                      slave_buf,
                      sizeof(slave_buf),
                      &slave_len);
    if (ret != 0)
    {
        printf("build slave frame failed: %d\n", ret);
        return 1;
    }

    printf("unix time              = %u\n", (unsigned)now);
    printf("\n");
    print_master_config(&master);
    printf("master payload size    = %u bytes\n", (unsigned)sizeof(master));
    printf("master frame size      = %u bytes\n", (unsigned)master_len);
    printf("master frame hex:\n");
    print_hex(master_buf, master_len);

    printf("\n");
    print_slave_config(&slave);
    printf("slave payload size     = %u bytes\n", (unsigned)sizeof(slave));
    printf("slave frame size       = %u bytes\n", (unsigned)slave_len);
    printf("slave frame hex:\n");
    print_hex(slave_buf, slave_len);

if (save_file("master_v2.bin", master_buf, master_len) != 0)
{
    printf("save master_v2.bin failed\n");
    return 1;
}

if (save_file("slave_v2.bin", slave_buf, slave_len) != 0)
{
    printf("save slave_v2.bin failed\n");
    return 1;
}
printf("save success\n");
#ifdef ftp2phone
if (upload_by_curl("master_v2.bin", "master_v2.bin") != 0)
{
    printf("upload master_v2.bin failed\n");
    return 1;
}

if (upload_by_curl("slave_v2.bin", "slave_v2.bin") != 0)
{
    printf("upload slave_v2.bin failed\n");
    return 1;
}

printf("upload success\n");
#endif
    return 0;
}
