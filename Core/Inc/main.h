/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

#include "st25dv_conf.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define E290_IRQ_Pin GPIO_PIN_1
#define E290_IRQ_GPIO_Port GPIOA
#define SPI_CS_Pin GPIO_PIN_4
#define SPI_CS_GPIO_Port GPIOA
#define E290_RF_SET_Pin GPIO_PIN_0
#define E290_RF_SET_GPIO_Port GPIOB
#define E290_GPIO11_Pin GPIO_PIN_1
#define E290_GPIO11_GPIO_Port GPIOB
#define GPO_Pin GPIO_PIN_2
#define GPO_GPIO_Port GPIOB
#define GPO_EXTI_IRQn EXTI2_IRQn
#define Senser_3_Pin GPIO_PIN_14
#define Senser_3_GPIO_Port GPIOB
#define Senser_4_Pin GPIO_PIN_15
#define Senser_4_GPIO_Port GPIOB
#define DE_485_Pin GPIO_PIN_8
#define DE_485_GPIO_Port GPIOA
#define Senser_1_Pin GPIO_PIN_5
#define Senser_1_GPIO_Port GPIOB
#define Senser_2_Pin GPIO_PIN_6
#define Senser_2_GPIO_Port GPIOB
#define BUZZER_Pin GPIO_PIN_7
#define BUZZER_GPIO_Port GPIOB
#define LED_G_Pin GPIO_PIN_8
#define LED_G_GPIO_Port GPIOB
#define LED_R_Pin GPIO_PIN_9
#define LED_R_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */
#define HARDWARE_V03
extern uint8_t BUS_MASTER_DEF;
extern uint8_t UART_DEBUG;
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
