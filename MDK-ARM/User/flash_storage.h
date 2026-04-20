#ifndef __FLASH_STORAGE_H__
#define __FLASH_STORAGE_H__

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_MAX_DATA_LEN      64U

/* STM32F103CB: 128KB Flash, 1KB per page */
#define STORAGE_PAGE_SIZE         1024U

/* 最后两页 */
#define STORAGE_PAGE0_ADDR        0x0801F800U
#define STORAGE_PAGE1_ADDR        0x0801FC00U

HAL_StatusTypeDef storage_init(void);
HAL_StatusTypeDef storage_save(const uint8_t *data, uint16_t len);
HAL_StatusTypeDef storage_load_latest(uint8_t *out, uint16_t *out_len);
bool storage_has_valid_data(void);
uint32_t storage_crc32(const uint8_t *data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif
