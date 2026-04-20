//EBIT_LC
#ifndef __E290_HAL_H_
#define __E290_HAL_H_
#include "main.h"

//EBIT_LC
typedef enum en_flag_status
{
    Reset = 0u,
    Set   = 1u,
} en_flag_status_t, en_int_status_t;
en_flag_status_t PORT_GetBit(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin);
#endif


