#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Frame format:
 * [SOF][TIME][TYPE][PAYLOAD][CRC32]
 *
 * SOF   : 1 byte, fixed 0xAA
 * TIME  : 4 bytes, unix timestamp in seconds, little-endian
 * TYPE  : 1 byte, master or slave
 * PAYLOAD:
 *   master = rf(8) + reserved(16) + business(21) = 45 bytes
 *   slave  = rf(8) + reserved(16) + business(2)  = 26 bytes
 * CRC32 : 4 bytes, calculated over SOF + TIME + TYPE + PAYLOAD
 *         using STM32-style CRC32 (word-fed, little-endian, 0xFF pad)
 */

/* ================= Business parameters (edit here) ================= */

#define DEFAULT_SLAVE_VEHICLE_ID          0x12
#define DEFAULT_SLAVE_ID                  0x00

#define DEFAULT_MASTER_VEHICLE_ID         0x12
#define DEFAULT_MASTER_POLL_START_SLAVE   0
#define DEFAULT_MASTER_POLL_END_SLAVE     1

/* ================= Frame constants ================= */

#define FRAME_SOF                         0xAA
#define FRAME_MASTER_TYPE                 0x01
#define FRAME_SLAVE_TYPE                  0x02
#define RESERVED_LEN                      16
#define RESERVED_VALID_MASK               0x01U

/* ================= RF default parameters ================= */

#define DEFAULT_RF_PWR                    22
#define DEFAULT_RF_FREQ                   470114814UL
#define DEFAULT_RF_SF                     7
#define DEFAULT_RF_BW                     8
#define DEFAULT_RF_CR                     2

/* ================= MASTER default parameters ================= */

#define DEFAULT_MASTER_MAX_RETRY          1
#define DEFAULT_MASTER_REPLY_TIMEOUT_MS   40
#define DEFAULT_MASTER_INTER_QUERY_GAP_MS 5
#define DEFAULT_MASTER_CONTEND_M          80
#define DEFAULT_MASTER_CAD_IDLE_TICK_MS   60
#define DEFAULT_MASTER_ROUND_SLEEP_MS     1000

#pragma pack(push, 1)

#define bit0 0
#define bit1 1
#define bit2 2
#define bit3 3
#define bit4 4
#define bit5 5
#define bit6 6
#define bit7 7


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
    rf_param_t rf;
    uint8_t reserved[RESERVED_LEN];

    uint8_t vehicle_id;
    uint8_t poll_start_slave;
    uint8_t poll_end_slave;
    uint8_t max_retry;

    uint32_t reply_timeout_ms;
    uint32_t inter_query_gap_ms;

    uint8_t contend_M;

    uint32_t cad_idle_tick_ms;
    uint32_t round_sleep_until_tick_ms;
} master_payload_t;

typedef struct
{
    rf_param_t rf;
    uint8_t reserved[RESERVED_LEN];

    uint8_t vehicle_id;
    uint8_t slave_id;
} slave_payload_t;

#pragma pack(pop)

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
//写字节内具体的一位
uint8_t set_bit(uint8_t val, uint8_t bit, uint8_t state) {
    // 简单的边界检查（可选，视具体需求而定）
    if (bit > 7) {
        return val; // 如果位数超过 7，直接返回原值
    }

    if (state) {
        // 置 1：使用按位或 (|)
        // 1 << bit 会生成如 00000100 的掩码
        return val | (1 << bit);
    } else {
        // 置 0：使用按位与 (&) 和 按位取反 (~)
        // ~(1 << bit) 会生成如 11111011 的掩码
        return val & ~(1 << bit);
    }
}

/**
 * @brief 设置指定位区间的值
 * 
 * @param val 原始数值
 * @param start_bit 起始位 (低位)
 * @param end_bit 终止位 (高位)
 * @param field_val 要写入的数值
 * @return uint8_t 修改后的数值
 */
uint8_t set_bits_range(uint8_t val, uint8_t start_bit, uint8_t end_bit, uint8_t field_val) {
    // 1. 简单的参数安全检查
    if (start_bit > 7 || end_bit > 7 || start_bit > end_bit) {
        return val; 
    }

    // 2. 计算位宽
    uint8_t width = end_bit - start_bit + 1;

    // 3. 核心步骤：先清零，再赋值
    
    // 步骤 A: 构造掩码并清零
    // ((1 << width) - 1) 会生成 width 个 1 (例如 width=3 -> 00000111)
    // 再左移 start_bit 对齐位置
    // 最后取反 (~) 用于清零操作
    uint8_t mask = ~(((1 << width) - 1) << start_bit);
    val = val & mask; 

    // 步骤 B: 将新值移位并填入
    // 这里我们做一个保护，防止传入的 field_val 超出位宽限制
    // 例如：宽度是3位，但传入了 1111 (15)，我们需要截断为 111 (7)
    field_val = field_val & ((1 << width) - 1);
    
    val = val | (field_val << start_bit);

    return val;
}

int main(void)
{
    uint32_t now = current_unix_time_u32();

    /* ===== Manual master configuration =====
     * reserved[0] bit0 = 1 means reserved segment is valid.
     * If you do not use reserved bytes yet, keeping only bit0 = 1 is fine.
     */
    master_payload_t master = {
        .rf = {
            .pwr  = DEFAULT_RF_PWR,
            .freq = DEFAULT_RF_FREQ,
            .sf   = DEFAULT_RF_SF,
            .bw   = DEFAULT_RF_BW,
            .cr   = DEFAULT_RF_CR
        },
        .reserved = {0},
        .vehicle_id = DEFAULT_MASTER_VEHICLE_ID,
        .poll_start_slave = DEFAULT_MASTER_POLL_START_SLAVE,
        .poll_end_slave = DEFAULT_MASTER_POLL_END_SLAVE,
        .max_retry = DEFAULT_MASTER_MAX_RETRY,
        .reply_timeout_ms = DEFAULT_MASTER_REPLY_TIMEOUT_MS,
        .inter_query_gap_ms = DEFAULT_MASTER_INTER_QUERY_GAP_MS,
        .contend_M = DEFAULT_MASTER_CONTEND_M,
        .cad_idle_tick_ms = DEFAULT_MASTER_CAD_IDLE_TICK_MS,
        .round_sleep_until_tick_ms = DEFAULT_MASTER_ROUND_SLEEP_MS
    };

    /* ===== Manual slave configuration ===== */
    slave_payload_t slave = {
        .rf = {
            .pwr  = DEFAULT_RF_PWR,
            .freq = DEFAULT_RF_FREQ,
            .sf   = DEFAULT_RF_SF,
            .bw   = DEFAULT_RF_BW,
            .cr   = DEFAULT_RF_CR
        },
        .reserved = {0},
        .vehicle_id = DEFAULT_SLAVE_VEHICLE_ID,
        .slave_id = DEFAULT_SLAVE_ID
    };

    uint8_t reserved_temp[RESERVED_LEN]={0};
    reserved_temp[0] = set_bit(reserved_temp[0],bit0,1);//置位设定reserved段是否有效。如果设定为0，则会从片内flash加载先前保存的上一次有效reserved段配置。如果先前从来没有配置过有效的reserved段配置，则不工作。
    reserved_temp[0] = set_bit(reserved_temp[0],bit1,0);//是否485接口日志输出，0为不输出
    reserved_temp[1] = 20;//设定蜂鸣器连续响的秒数。设定0秒不响，最大设定254秒，设定0xff则一直响。
    reserved_temp[2] = 1;//设定没系安全带多长时间就开始响，最大设定255秒.
    
    memcpy(master.reserved,reserved_temp,RESERVED_LEN);
    memcpy(slave.reserved,reserved_temp,RESERVED_LEN);

    uint32_t master_time = now;
    uint32_t slave_time  = now;

    uint8_t master_buf[128];
    uint8_t slave_buf[128];
    uint16_t master_len = 0;
    uint16_t slave_len = 0;

    int ret;

    ret = build_frame(FRAME_MASTER_TYPE,
                      master_time,
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
                      slave_time,
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

    printf("unix time            = %u\n", (unsigned)now);
    printf("master payload size  = %u bytes\n", (unsigned)sizeof(master));
    printf("master frame size    = %u bytes\n", (unsigned)master_len);
    printf("master frame hex:\n");
    print_hex(master_buf, master_len);

    printf("\n");

    printf("slave payload size   = %u bytes\n", (unsigned)sizeof(slave));
    printf("slave frame size     = %u bytes\n", (unsigned)slave_len);
    printf("slave frame hex:\n");
    print_hex(slave_buf, slave_len);

    if (save_file("master.bin", master_buf, master_len) != 0)
    {
        printf("save master.bin failed\n");
        return 1;
    }

    if (save_file("slave.bin", slave_buf, slave_len) != 0)
    {
        printf("save slave.bin failed\n");
        return 1;
    }

    printf("\nGenerated files:\n");
    printf("  master.bin\n");
    printf("  slave.bin\n");

    return 0;
}
