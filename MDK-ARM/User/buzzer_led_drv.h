#ifndef BUZZER_LED_DRV_H
#define BUZZER_LED_DRV_H

#include <stdint.h>

/* 模式定义 */
typedef enum
{
    BUZZER_LED_MODE_OFF = 0,
    BUZZER_LED_MODE_ON,
    BUZZER_LED_MODE_SLOW_BLINK,
    BUZZER_LED_MODE_FAST_BLINK
} buzzer_led_mode_t;

/* 设备选择 */
typedef enum
{
    BUZZER_LED_DEV_BUZZER = 0,
    BUZZER_LED_DEV_LED_G,
    BUZZER_LED_DEV_LED_R,
    BUZZER_LED_DEV_MAX
} buzzer_led_dev_t;

/* 初始化 */
void BuzzerLed_Init(void);

/* 设置模式 */
void BuzzerLed_SetMode(buzzer_led_dev_t dev, buzzer_led_mode_t mode);

/* 获取当前模式 */
buzzer_led_mode_t BuzzerLed_GetMode(buzzer_led_dev_t dev);

/* 快捷控制接口 */
void BuzzerLed_On(buzzer_led_dev_t dev);
void BuzzerLed_Off(buzzer_led_dev_t dev);
void BuzzerLed_Toggle(buzzer_led_dev_t dev);

/* 10Hz 定时器调用 */
void BuzzerLed_Tick10Hz(void);

#endif
