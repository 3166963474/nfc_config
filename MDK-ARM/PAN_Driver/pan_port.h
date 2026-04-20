/*******************************************************************************
 * @note Copyright (C) 2023 Shanghai Panchip Microelectronics Co., Ltd. All rights reserved.
 *
 * @file pan_port.h
 * @brief
 *
 * @history - V0.8, 2024-4
*******************************************************************************/
#ifndef __rf_PORT_H_
#define __rf_PORT_H_
#include "stdio.h"
#include "stdint.h"
#include "stdlib.h"
#include "stdbool.h"
#include "math.h"
#include "pan_rf.h"

#include "stm32f1xx_hal.h"
#include "main.h"

#define SPI_WRITE_CHECK         1
#define MODULE_GPIO_TX          0
#define MODULE_GPIO_RX          10
#define MODULE_GPIO_TCXO        3
#define MODULE_GPIO_CAD_IRQ     11

typedef struct {
    void (*antenna_init)(void);
    void (*tcxo_init)(void);
    void (*set_tx)(void);
    void (*set_rx)(void);
    void (*antenna_close)(void);
    void (*tcxo_close)(void);
    uint8_t (*spi_readwrite)(uint8_t tx_data);
    void (*spi_cs_high)(void);
    void (*spi_cs_low)(void);
    void (*delayms)(uint32_t time);
    void (*delayus)(uint32_t time);
} rf_port_t;

extern rf_port_t rf_port;

uint8_t spi_readwritebyte(uint8_t tx_data);
void spi_cs_set_high(void);
void spi_cs_set_low(void);
void rf_delay_ms(uint32_t time);
void rf_delay_us(uint32_t time);
void rf_antenna_init(void);
void rf_tcxo_init(void);
void rf_tcxo_close(void);
void rf_antenna_rx(void);
void rf_antenna_tx(void);
void rf_antenna_close(void);

#endif


