#include "main.h"
#include <stdint.h>
#include "buzzer_led_drv.h"
/*
 * buzzer_led_drv.c
 *
 * 说明：
 * 1. 本文件同时实现蜂鸣器、绿灯、红灯驱动。
 * 2. LED 为低电平点亮，蜂鸣器为高电平鸣叫。
 * 3. 需要在 10Hz 定时器中断中周期调用 BuzzerLed_Tick10Hz()。
 *
 * 引脚定义（已由用户提供）：
 * #define BUZZER_Pin       GPIO_PIN_7
 * #define BUZZER_GPIO_Port GPIOB
 * #define LED_G_Pin        GPIO_PIN_8
 * #define LED_G_GPIO_Port  GPIOB
 * #define LED_R_Pin        GPIO_PIN_9
 * #define LED_R_GPIO_Port  GPIOB
 *
 * 推荐用法：
 *
 *   BuzzerLed_Init();
 *   BuzzerLed_SetMode(BUZZER_LED_DEV_LED_G, BUZZER_LED_MODE_SLOW_BLINK);
 *   BuzzerLed_SetMode(BUZZER_LED_DEV_LED_R, BUZZER_LED_MODE_OFF);
 *   BuzzerLed_SetMode(BUZZER_LED_DEV_BUZZER, BUZZER_LED_MODE_FAST_BLINK);
 *
 *   void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
 *   {
 *       if (htim->Instance == TIMx)
 *       {
 *           BuzzerLed_Tick10Hz();
 *       }
 *   }
 */

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t active_level;
    buzzer_led_mode_t mode;
    uint8_t output_on;
    uint8_t tick_div_cnt;
} buzzer_led_ctrl_t;

#define BUZZER_LED_ACTIVE_HIGH    1u
#define BUZZER_LED_ACTIVE_LOW     0u

#define BUZZER_LED_SLOW_TOGGLE_TICKS   5u
#define BUZZER_LED_FAST_TOGGLE_TICKS   1u

static buzzer_led_ctrl_t s_buzzer_led_list[BUZZER_LED_DEV_MAX] =
{
    [BUZZER_LED_DEV_BUZZER] = {
        .port = BUZZER_GPIO_Port,
        .pin = BUZZER_Pin,
        .active_level = BUZZER_LED_ACTIVE_HIGH,
        .mode = BUZZER_LED_MODE_OFF,
        .output_on = 0u,
        .tick_div_cnt = 0u
    },
    [BUZZER_LED_DEV_LED_G] = {
        .port = LED_G_GPIO_Port,
        .pin = LED_G_Pin,
        .active_level = BUZZER_LED_ACTIVE_LOW,
        .mode = BUZZER_LED_MODE_OFF,
        .output_on = 0u,
        .tick_div_cnt = 0u
    },
    [BUZZER_LED_DEV_LED_R] = {
        .port = LED_R_GPIO_Port,
        .pin = LED_R_Pin,
        .active_level = BUZZER_LED_ACTIVE_LOW,
        .mode = BUZZER_LED_MODE_OFF,
        .output_on = 0u,
        .tick_div_cnt = 0u
    }
};

static void BuzzerLed_WriteRaw(const buzzer_led_ctrl_t *dev, uint8_t on)
{
    GPIO_PinState pin_state;

    if (dev == 0)
    {
        return;
    }

    if (dev->active_level == BUZZER_LED_ACTIVE_HIGH)
    {
        pin_state = (on != 0u) ? GPIO_PIN_SET : GPIO_PIN_RESET;
    }
    else
    {
        pin_state = (on != 0u) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }

    HAL_GPIO_WritePin(dev->port, dev->pin, pin_state);
}

static void BuzzerLed_ApplyOutput(buzzer_led_ctrl_t *dev, uint8_t on)
{
    if (dev == 0)
    {
        return;
    }

    dev->output_on = (on != 0u) ? 1u : 0u;
    BuzzerLed_WriteRaw(dev, dev->output_on);
}

static uint8_t BuzzerLed_GetToggleTicks(buzzer_led_mode_t mode)
{
    switch (mode)
    {
        case BUZZER_LED_MODE_SLOW_BLINK:
            return BUZZER_LED_SLOW_TOGGLE_TICKS;

        case BUZZER_LED_MODE_FAST_BLINK:
            return BUZZER_LED_FAST_TOGGLE_TICKS;

        case BUZZER_LED_MODE_OFF:
        case BUZZER_LED_MODE_ON:
        default:
            return 0u;
    }
}

static void BuzzerLed_UpdateOne(buzzer_led_ctrl_t *dev)
{
    uint8_t toggle_ticks;

    if (dev == 0)
    {
        return;
    }

    switch (dev->mode)
    {
        case BUZZER_LED_MODE_OFF:
            BuzzerLed_ApplyOutput(dev, 0u);
            dev->tick_div_cnt = 0u;
            break;

        case BUZZER_LED_MODE_ON:
            BuzzerLed_ApplyOutput(dev, 1u);
            dev->tick_div_cnt = 0u;
            break;

        case BUZZER_LED_MODE_SLOW_BLINK:
        case BUZZER_LED_MODE_FAST_BLINK:
            toggle_ticks = BuzzerLed_GetToggleTicks(dev->mode);
            if (toggle_ticks == 0u)
            {
                BuzzerLed_ApplyOutput(dev, 0u);
                dev->tick_div_cnt = 0u;
                break;
            }

            dev->tick_div_cnt++;
            if (dev->tick_div_cnt >= toggle_ticks)
            {
                dev->tick_div_cnt = 0u;
                BuzzerLed_ApplyOutput(dev, (uint8_t)!dev->output_on);
            }
            break;

        default:
            BuzzerLed_ApplyOutput(dev, 0u);
            dev->tick_div_cnt = 0u;
            break;
    }
}

void BuzzerLed_Init(void)
{
    uint8_t i;

    for (i = 0u; i < (uint8_t)BUZZER_LED_DEV_MAX; i++)
    {
        s_buzzer_led_list[i].mode = BUZZER_LED_MODE_OFF;
        s_buzzer_led_list[i].output_on = 0u;
        s_buzzer_led_list[i].tick_div_cnt = 0u;
        BuzzerLed_WriteRaw(&s_buzzer_led_list[i], 0u);
    }
}

void BuzzerLed_SetMode(buzzer_led_dev_t dev, buzzer_led_mode_t mode)
{
    if ((uint8_t)dev >= (uint8_t)BUZZER_LED_DEV_MAX)
    {
        return;
    }

    s_buzzer_led_list[dev].mode = mode;
    s_buzzer_led_list[dev].tick_div_cnt = 0u;

    switch (mode)
    {
        case BUZZER_LED_MODE_OFF:
            BuzzerLed_ApplyOutput(&s_buzzer_led_list[dev], 0u);
            break;

        case BUZZER_LED_MODE_ON:
            BuzzerLed_ApplyOutput(&s_buzzer_led_list[dev], 1u);
            break;

        case BUZZER_LED_MODE_SLOW_BLINK:
        case BUZZER_LED_MODE_FAST_BLINK:
        default:
            BuzzerLed_ApplyOutput(&s_buzzer_led_list[dev], 1u);
            break;
    }
}

buzzer_led_mode_t BuzzerLed_GetMode(buzzer_led_dev_t dev)
{
    if ((uint8_t)dev >= (uint8_t)BUZZER_LED_DEV_MAX)
    {
        return BUZZER_LED_MODE_OFF;
    }

    return s_buzzer_led_list[dev].mode;
}

void BuzzerLed_On(buzzer_led_dev_t dev)
{
    BuzzerLed_SetMode(dev, BUZZER_LED_MODE_ON);
}

void BuzzerLed_Off(buzzer_led_dev_t dev)
{
    BuzzerLed_SetMode(dev, BUZZER_LED_MODE_OFF);
}

void BuzzerLed_Toggle(buzzer_led_dev_t dev)
{
    if ((uint8_t)dev >= (uint8_t)BUZZER_LED_DEV_MAX)
    {
        return;
    }

    BuzzerLed_ApplyOutput(&s_buzzer_led_list[dev],
                          (uint8_t)!s_buzzer_led_list[dev].output_on);
}

void BuzzerLed_Tick10Hz(void)
{
    uint8_t i;

    for (i = 0u; i < (uint8_t)BUZZER_LED_DEV_MAX; i++)
    {
        BuzzerLed_UpdateOne(&s_buzzer_led_list[i]);
    }
}
