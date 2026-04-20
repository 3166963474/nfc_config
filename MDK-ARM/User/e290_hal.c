#include "e290_hal.h"

en_flag_status_t PORT_GetBit(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin)
{
    return (HAL_GPIO_ReadPin(GPIOx,GPIO_Pin) == GPIO_PIN_SET) ? Set : Reset;
}
