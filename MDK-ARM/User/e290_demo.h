//EBIT_LC
#ifndef __E290_DEMO_H_
#define __E290_DEMO_H_
#include "main.h"
#include "spi.h"
#include "stm32f1xx_hal.h"
#include "tim.h"
#include "string.h"
#include "pan_rf.h"
#include "rs485bsp.h"

//EBIT_LC
uint8_t spi_readwritebyte(uint8_t txdata);
void TIM_Delay_us(uint32_t us);
_Bool e290_demo_check_rx_done( uint8_t *buffer, uint8_t * length  , double *rssi);
void e290_demo_receive( void );
void e290_demo_transmit( uint8_t *buffer , uint8_t length );
void e290_demo_task(void);
#endif


