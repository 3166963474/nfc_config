#include "flash_storage.h"
#include <string.h>
extern CRC_HandleTypeDef hcrc;
/* =========================
 * 配置区
 * ========================= */
#define STORAGE_PAGE_MAGIC        0x5047A55AU  /* "PG" + magic */
#define STORAGE_RECORD_MAGIC      0x5243A55AU  /* "RC" + magic */

#define PAGE_STATE_ERASED         0xFFFFFFFFU
#define PAGE_STATE_ACTIVE         0xA5A5A5A5U
#define PAGE_STATE_OLD            0x00000000U

#define RECORD_STATE_WRITING      0xFFFFFFFFU
#define RECORD_STATE_VALID        0x00000000U

#define STORAGE_ALIGN             4U

#define PAGE_HEADER_SIZE          ((uint32_t)sizeof(page_header_t))
#define RECORD_HEADER_SIZE        ((uint32_t)sizeof(record_header_t))

/* =========================
 * 数据结构
 * ========================= */
typedef struct
{
    uint32_t magic;
    uint32_t state;
    uint32_t seq;
} page_header_t;

typedef struct
{
    uint32_t magic;
    uint32_t state;
    uint32_t len;
    uint32_t version;
    uint32_t crc32;
} record_header_t;

typedef struct
{
    uint32_t active_page_addr;
    uint32_t active_page_seq;
    uint32_t next_version;
    uint32_t latest_record_addr;
    uint16_t latest_len;
    bool has_valid;
} storage_ctx_t;

static storage_ctx_t g_storage_ctx;

/* =========================
 * 工具函数
 * ========================= */
static uint32_t align_up4(uint32_t x)
{
    return (x + 3U) & ~3U;
}

static uint32_t record_total_size(uint16_t len)
{
    return RECORD_HEADER_SIZE + align_up4(len);
}

static uint32_t page_end_addr(uint32_t page_addr)
{
    return page_addr + STORAGE_PAGE_SIZE;
}

static uint32_t other_page_addr(uint32_t addr)
{
    return (addr == STORAGE_PAGE0_ADDR) ? STORAGE_PAGE1_ADDR : STORAGE_PAGE0_ADDR;
}

static bool is_addr_in_page(uint32_t page_addr, uint32_t addr)
{
    return (addr >= page_addr) && (addr < (page_addr + STORAGE_PAGE_SIZE));
}

static bool flash_is_erased_u32(uint32_t addr)
{
    return (*(const uint32_t *)addr == 0xFFFFFFFFU);
}

static bool area_is_all_ff(uint32_t addr, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i += 4U)
    {
        if (*(const uint32_t *)(addr + i) != 0xFFFFFFFFU)
        {
            return false;
        }
    }
    return true;
}

/* =========================
 * CRC32（使用硬件CRC）
 * F1 CRC 按 32bit 喂数
 * 不足4字节补0xFF
 * ========================= */
uint32_t storage_crc32(const uint8_t *data, uint16_t len)
{
    uint32_t word;
    uint16_t i = 0U;

    __HAL_CRC_DR_RESET(&hcrc);

    while (i < len)
    {
        word = 0xFFFFFFFFU;

        ((uint8_t *)&word)[0] = data[i++];
        if (i < len) ((uint8_t *)&word)[1] = data[i++];
        if (i < len) ((uint8_t *)&word)[2] = data[i++];
        if (i < len) ((uint8_t *)&word)[3] = data[i++];

        HAL_CRC_Accumulate(&hcrc, &word, 1U);
    }

    return hcrc.Instance->DR;
}

/* 重新从Flash读数据算CRC */
static uint32_t storage_crc32_flash(uint32_t data_addr, uint16_t len)
{
    uint8_t buf[STORAGE_MAX_DATA_LEN];
    uint16_t padded_len = (uint16_t)align_up4(len);

    if (len > STORAGE_MAX_DATA_LEN)
    {
        return 0U;
    }

    memcpy(buf, (const void *)data_addr, len);

    /* 注意：CRC只校验真实长度，不校验padding */
    return storage_crc32(buf, len);
}

/* =========================
 * Flash底层写函数
 * F1: HalfWord编程
 * ========================= */
static HAL_StatusTypeDef flash_program_u16(uint32_t addr, uint16_t value)
{
    return HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, value);
}

static HAL_StatusTypeDef flash_program_u32(uint32_t addr, uint32_t value)
{
    HAL_StatusTypeDef ret;

    ret = flash_program_u16(addr, (uint16_t)(value & 0xFFFFU));
    if (ret != HAL_OK) return ret;

    ret = flash_program_u16(addr + 2U, (uint16_t)((value >> 16) & 0xFFFFU));
    return ret;
}

static HAL_StatusTypeDef flash_write_buf(uint32_t addr, const uint8_t *buf, uint32_t len_padded)
{
    uint32_t i;
    uint16_t halfword;
    uint8_t b0, b1;

    for (i = 0; i < len_padded; i += 2U)
    {
        b0 = buf[i];
        b1 = buf[i + 1U];
        halfword = (uint16_t)b0 | ((uint16_t)b1 << 8);

        if (flash_program_u16(addr + i, halfword) != HAL_OK)
        {
            return HAL_ERROR;
        }
    }

    return HAL_OK;
}

static HAL_StatusTypeDef flash_erase_page(uint32_t page_addr)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0U;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = page_addr;
    erase.NbPages = 1U;

    return HAL_FLASHEx_Erase(&erase, &page_error);
}

/* =========================
 * Page头处理
 * ========================= */
static bool page_header_is_valid(uint32_t page_addr, page_header_t *hdr)
{
    memcpy(hdr, (const void *)page_addr, sizeof(page_header_t));

    if (hdr->magic != STORAGE_PAGE_MAGIC)
    {
        return false;
    }

    if ((hdr->state != PAGE_STATE_ACTIVE) &&
        (hdr->state != PAGE_STATE_OLD))
    {
        return false;
    }

    return true;
}

static HAL_StatusTypeDef page_write_header(uint32_t page_addr, uint32_t state, uint32_t seq)
{
    HAL_StatusTypeDef ret;
    page_header_t hdr;

    hdr.magic = STORAGE_PAGE_MAGIC;
    hdr.state = state;
    hdr.seq = seq;

    ret = flash_program_u32(page_addr + 0U, hdr.magic);
    if (ret != HAL_OK) return ret;

    ret = flash_program_u32(page_addr + 4U, hdr.state);
    if (ret != HAL_OK) return ret;

    ret = flash_program_u32(page_addr + 8U, hdr.seq);
    return ret;
}

/* 仅把ACTIVE页改成OLD，1->0安全 */
static HAL_StatusTypeDef page_mark_old(uint32_t page_addr)
{
    return flash_program_u32(page_addr + 4U, PAGE_STATE_OLD);
}

/* =========================
 * Record扫描
 * ========================= */
typedef struct
{
    bool found;
    uint32_t record_addr;
    uint32_t version;
    uint16_t len;
    uint32_t next_free_addr;
} scan_result_t;

static bool record_is_valid(uint32_t rec_addr, record_header_t *hdr)
{
    uint32_t crc;

    memcpy(hdr, (const void *)rec_addr, sizeof(record_header_t));

    if (hdr->magic != STORAGE_RECORD_MAGIC)
    {
        return false;
    }

    if (hdr->state != RECORD_STATE_VALID)
    {
        return false;
    }

    if ((hdr->len == 0U) || (hdr->len > STORAGE_MAX_DATA_LEN))
    {
        return false;
    }

    uint32_t page_addr;

    if (is_addr_in_page(STORAGE_PAGE0_ADDR, rec_addr))
    {
        page_addr = STORAGE_PAGE0_ADDR;
    }
    else if (is_addr_in_page(STORAGE_PAGE1_ADDR, rec_addr))
    {
        page_addr = STORAGE_PAGE1_ADDR;
    }
    else
    {
        return false;
    }

    if ((rec_addr + record_total_size((uint16_t)hdr->len)) > page_end_addr(page_addr))
    {
        return false;
    }

    crc = storage_crc32_flash(rec_addr + RECORD_HEADER_SIZE, (uint16_t)hdr->len);
    if (crc != hdr->crc32)
    {
        return false;
    }

    return true;
}

static scan_result_t scan_page_records(uint32_t page_addr)
{
    scan_result_t res;
    uint32_t addr;
    record_header_t hdr;
    uint32_t end_addr = page_addr + STORAGE_PAGE_SIZE;

    memset(&res, 0, sizeof(res));
    addr = page_addr + PAGE_HEADER_SIZE;

    while ((addr + RECORD_HEADER_SIZE) <= end_addr)
    {
        /* 遇到全FF，认为后面是空闲 */
        if (flash_is_erased_u32(addr))
        {
            res.next_free_addr = addr;
            return res;
        }

        memcpy(&hdr, (const void *)addr, sizeof(hdr));

        /* magic不对，停止扫描，认为后面不可用 */
        if (hdr.magic != STORAGE_RECORD_MAGIC)
        {
            res.next_free_addr = addr;
            return res;
        }

        /* len非法，也停止 */
        if ((hdr.len == 0U) || (hdr.len > STORAGE_MAX_DATA_LEN))
        {
            res.next_free_addr = addr;
            return res;
        }

        if ((addr + record_total_size((uint16_t)hdr.len)) > end_addr)
        {
            res.next_free_addr = addr;
            return res;
        }

        if (record_is_valid(addr, &hdr))
        {
            if ((!res.found) || (hdr.version > res.version))
            {
                res.found = true;
                res.record_addr = addr;
                res.version = hdr.version;
                res.len = (uint16_t)hdr.len;
            }
        }

        addr += record_total_size((uint16_t)hdr.len);
    }

    res.next_free_addr = addr;
    return res;
}

/* =========================
 * 查找活动页
 * ========================= */
static HAL_StatusTypeDef storage_format_pages(void)
{
    HAL_StatusTypeDef ret;

    HAL_FLASH_Unlock();

    ret = flash_erase_page(STORAGE_PAGE0_ADDR);
    if (ret != HAL_OK) goto out;

    ret = flash_erase_page(STORAGE_PAGE1_ADDR);
    if (ret != HAL_OK) goto out;

    ret = page_write_header(STORAGE_PAGE0_ADDR, PAGE_STATE_ACTIVE, 1U);
    if (ret != HAL_OK) goto out;

out:
    HAL_FLASH_Lock();
    return ret;
}

static HAL_StatusTypeDef storage_select_active_page(void)
{
    page_header_t h0, h1;
    bool v0, v1;
    scan_result_t s0, s1;

    memset(&g_storage_ctx, 0, sizeof(g_storage_ctx));

    v0 = page_header_is_valid(STORAGE_PAGE0_ADDR, &h0);
    v1 = page_header_is_valid(STORAGE_PAGE1_ADDR, &h1);

    if (!v0 && !v1)
    {
        if (storage_format_pages() != HAL_OK)
        {
            return HAL_ERROR;
        }

        g_storage_ctx.active_page_addr = STORAGE_PAGE0_ADDR;
        g_storage_ctx.active_page_seq = 1U;
        g_storage_ctx.next_version = 1U;
        g_storage_ctx.latest_record_addr = 0U;
        g_storage_ctx.latest_len = 0U;
        g_storage_ctx.has_valid = false;
        return HAL_OK;
    }

    if (v0 && (!v1))
    {
        g_storage_ctx.active_page_addr = (h0.state == PAGE_STATE_ACTIVE) ? STORAGE_PAGE0_ADDR : STORAGE_PAGE0_ADDR;
        g_storage_ctx.active_page_seq = h0.seq;
    }
    else if ((!v0) && v1)
    {
        g_storage_ctx.active_page_addr = (h1.state == PAGE_STATE_ACTIVE) ? STORAGE_PAGE1_ADDR : STORAGE_PAGE1_ADDR;
        g_storage_ctx.active_page_seq = h1.seq;
    }
    else
    {
        /* 两页都有效时，优先ACTIVE；若都能看作有效，则seq大的为新 */
        if ((h0.state == PAGE_STATE_ACTIVE) && (h1.state != PAGE_STATE_ACTIVE))
        {
            g_storage_ctx.active_page_addr = STORAGE_PAGE0_ADDR;
            g_storage_ctx.active_page_seq = h0.seq;
        }
        else if ((h1.state == PAGE_STATE_ACTIVE) && (h0.state != PAGE_STATE_ACTIVE))
        {
            g_storage_ctx.active_page_addr = STORAGE_PAGE1_ADDR;
            g_storage_ctx.active_page_seq = h1.seq;
        }
        else
        {
            if (h0.seq >= h1.seq)
            {
                g_storage_ctx.active_page_addr = STORAGE_PAGE0_ADDR;
                g_storage_ctx.active_page_seq = h0.seq;
            }
            else
            {
                g_storage_ctx.active_page_addr = STORAGE_PAGE1_ADDR;
                g_storage_ctx.active_page_seq = h1.seq;
            }
        }
    }

    s0 = scan_page_records(STORAGE_PAGE0_ADDR);
    s1 = scan_page_records(STORAGE_PAGE1_ADDR);

    g_storage_ctx.has_valid = false;
    g_storage_ctx.latest_record_addr = 0U;
    g_storage_ctx.latest_len = 0U;
    g_storage_ctx.next_version = 1U;

    if (s0.found && (!s1.found || (s0.version >= s1.version)))
    {
        g_storage_ctx.has_valid = true;
        g_storage_ctx.latest_record_addr = s0.record_addr;
        g_storage_ctx.latest_len = s0.len;
        g_storage_ctx.next_version = s0.version + 1U;
    }
    else if (s1.found)
    {
        g_storage_ctx.has_valid = true;
        g_storage_ctx.latest_record_addr = s1.record_addr;
        g_storage_ctx.latest_len = s1.len;
        g_storage_ctx.next_version = s1.version + 1U;
    }

    return HAL_OK;
}

/* =========================
 * 查找页内空闲位置
 * ========================= */
static uint32_t page_find_next_free(uint32_t page_addr)
{
    scan_result_t res = scan_page_records(page_addr);
    return res.next_free_addr;
}

/* =========================
 * 在指定页追加一条记录
 * ========================= */
static HAL_StatusTypeDef page_append_record(uint32_t page_addr,
                                            uint32_t version,
                                            const uint8_t *data,
                                            uint16_t len,
                                            uint32_t *out_record_addr)
{
    HAL_StatusTypeDef ret;
    uint32_t addr;
    uint32_t total_size;
    uint32_t padded_len;
    record_header_t hdr;
    uint8_t write_buf[align_up4(STORAGE_MAX_DATA_LEN)];

    if ((data == NULL) || (len == 0U) || (len > STORAGE_MAX_DATA_LEN))
    {
        return HAL_ERROR;
    }

    addr = page_find_next_free(page_addr);
    total_size = record_total_size(len);
    padded_len = align_up4(len);

    if ((addr + total_size) > (page_addr + STORAGE_PAGE_SIZE))
    {
        return HAL_ERROR;
    }

    memset(write_buf, 0xFF, sizeof(write_buf));
    memcpy(write_buf, data, len);

    hdr.magic = STORAGE_RECORD_MAGIC;
    hdr.state = RECORD_STATE_WRITING;
    hdr.len = len;
    hdr.version = version;
    hdr.crc32 = storage_crc32(data, len);

    HAL_FLASH_Unlock();

    /* 1. 写header，state先是WRITING */
    ret = flash_program_u32(addr + 0U,  hdr.magic);
    if (ret != HAL_OK) goto out;

    ret = flash_program_u32(addr + 4U,  hdr.state);
    if (ret != HAL_OK) goto out;

    ret = flash_program_u32(addr + 8U,  hdr.len);
    if (ret != HAL_OK) goto out;

    ret = flash_program_u32(addr + 12U, hdr.version);
    if (ret != HAL_OK) goto out;

    ret = flash_program_u32(addr + 16U, hdr.crc32);
    if (ret != HAL_OK) goto out;

    /* 2. 写data */
    ret = flash_write_buf(addr + RECORD_HEADER_SIZE, write_buf, padded_len);
    if (ret != HAL_OK) goto out;

    /* 3. 校验CRC */
    if (storage_crc32_flash(addr + RECORD_HEADER_SIZE, len) != hdr.crc32)
    {
        ret = HAL_ERROR;
        goto out;
    }

    /* 4. 最后写state为VALID */
    ret = flash_program_u32(addr + 4U, RECORD_STATE_VALID);
    if (ret != HAL_OK) goto out;

    if (*(const uint32_t *)(addr + 4U) != RECORD_STATE_VALID)
    {
        ret = HAL_ERROR;
        goto out;
    }

    if (out_record_addr != NULL)
    {
        *out_record_addr = addr;
    }

out:
    HAL_FLASH_Lock();
    return ret;
}

/* =========================
 * 页切换：把最新有效记录搬到新页
 * ========================= */
static HAL_StatusTypeDef storage_page_swap_and_save(const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef ret;
    uint32_t new_page;
    uint32_t old_page;

    old_page = g_storage_ctx.active_page_addr;
    new_page = other_page_addr(old_page);

    HAL_FLASH_Unlock();

    ret = flash_erase_page(new_page);
    if (ret != HAL_OK) goto out_unlock;

    ret = page_write_header(new_page, PAGE_STATE_ACTIVE, g_storage_ctx.active_page_seq + 1U);
    if (ret != HAL_OK) goto out_unlock;

    HAL_FLASH_Lock();

    ret = page_append_record(new_page, g_storage_ctx.next_version, data, len, &g_storage_ctx.latest_record_addr);
    if (ret != HAL_OK)
    {
        return ret;
    }

    HAL_FLASH_Unlock();

    ret = page_mark_old(old_page);
    if (ret != HAL_OK) goto out_unlock;

    ret = flash_erase_page(old_page);
    if (ret != HAL_OK) goto out_unlock;

    HAL_FLASH_Lock();

    g_storage_ctx.active_page_addr = new_page;
    g_storage_ctx.active_page_seq += 1U;
    g_storage_ctx.latest_len = len;
    g_storage_ctx.next_version += 1U;
    g_storage_ctx.has_valid = true;

    return HAL_OK;

out_unlock:
    HAL_FLASH_Lock();
    return ret;
}

/* =========================
 * 对外接口
 * ========================= */
HAL_StatusTypeDef storage_init()
{
	return storage_select_active_page();
}

bool storage_has_valid_data(void)
{
    return g_storage_ctx.has_valid;
}

HAL_StatusTypeDef storage_save(const uint8_t *data, uint16_t len)
{
    HAL_StatusTypeDef ret;
    uint32_t free_addr;
    uint32_t need_size;

    if ((data == NULL) || (len == 0U) || (len > STORAGE_MAX_DATA_LEN))
    {
        return HAL_ERROR;
    }

    free_addr = page_find_next_free(g_storage_ctx.active_page_addr);
    need_size = record_total_size(len);

    if ((free_addr + need_size) <= (g_storage_ctx.active_page_addr + STORAGE_PAGE_SIZE))
    {
        ret = page_append_record(g_storage_ctx.active_page_addr,
                                 g_storage_ctx.next_version,
                                 data,
                                 len,
                                 &g_storage_ctx.latest_record_addr);
        if (ret == HAL_OK)
        {
            g_storage_ctx.latest_len = len;
            g_storage_ctx.next_version += 1U;
            g_storage_ctx.has_valid = true;
        }
        return ret;
    }

    return storage_page_swap_and_save(data, len);
}

HAL_StatusTypeDef storage_load_latest(uint8_t *out, uint16_t *out_len)
{
    record_header_t hdr;

    if ((out == NULL) || (out_len == NULL))
    {
        return HAL_ERROR;
    }

    if ((!g_storage_ctx.has_valid) || (g_storage_ctx.latest_record_addr == 0U))
    {
        return HAL_ERROR;
    }

    memcpy(&hdr, (const void *)g_storage_ctx.latest_record_addr, sizeof(hdr));

    if (!record_is_valid(g_storage_ctx.latest_record_addr, &hdr))
    {
        return HAL_ERROR;
    }

    memcpy(out,
           (const void *)(g_storage_ctx.latest_record_addr + RECORD_HEADER_SIZE),
           hdr.len);

    *out_len = (uint16_t)hdr.len;
    return HAL_OK;
}
