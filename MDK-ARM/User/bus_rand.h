#ifndef __BUS_RAND_H
#define __BUS_RAND_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/**
 * @brief 初始化随机模块（可选）
 */
void bus_rand_init(void);

/**
 * @brief 获取一个32位随机数
 */
uint32_t bus_rand32(void);

/**
 * @brief 获取 [0, M-1] 之间均匀分布的随机数
 */
uint8_t bus_rand_slot(uint8_t M);

#endif
