#include "bus_rand.h"

/* 外部ADC句柄（你工程里已有） */
extern ADC_HandleTypeDef hadc1;

/* 内部状态 */
static uint32_t rand_state = 0xA5C3E27F;
static uint16_t last_adc = 0;

/* ================= 内部函数 ================= */

/* 32位混合函数（打散熵） */
static uint32_t rand_mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

/* 获取一次ADC采样（等待转换完成） */
static uint16_t rand_adc_sample(void)
{
    HAL_ADC_Start(&hadc1);
    HAL_ADC_PollForConversion(&hadc1, 10);
    return (uint16_t)HAL_ADC_GetValue(&hadc1);
}

/* 从ADC提取随机性并生成32位 */
static uint32_t rand_adc32(void)
{
    uint32_t x = rand_state;

    /* 多次采样增强随机性 */
    for (uint8_t i = 0; i < 32; i++)
    {
        uint16_t cur  = rand_adc_sample();
        uint16_t diff = (uint16_t)(cur - last_adc);
        last_adc = cur;

        /* 混合当前值 + 差分 */
        x ^= ((uint32_t)cur  << (i & 15));
        x ^= ((uint32_t)diff << ((i * 7) & 15));

        /* xorshift 扰动 */
        x ^= (x << 13);
        x ^= (x >> 17);
        x ^= (x << 5);

        /* 再做一次强混合 */
        x = rand_mix32(x ^ cur ^ ((uint32_t)diff << 16));
    }

    /* 更新内部状态 */
    rand_state ^= x + 0x9E3779B9U;
    rand_state = rand_mix32(rand_state);

    return rand_state;
}

/* ================= 对外接口 ================= */

void bus_rand_init(void)
{
    /* 用几次ADC扰动初始状态 */
    for (uint8_t i = 0; i < 16; i++)
    {
        rand_state ^= rand_adc_sample();
        rand_state = rand_mix32(rand_state);
    }
}

uint32_t bus_rand32(void)
{
    return rand_adc32();
}

uint8_t bus_rand_slot(uint8_t M)
{
    if (M <= 1)
    {
        return 0;
    }

    /* 拒绝采样避免取模偏差 */
    uint32_t limit = 0xFFFFFFFFU - (0xFFFFFFFFU % M);
    uint32_t r;

    do
    {
        r = bus_rand32();
    } while (r >= limit);

    return (uint8_t)(r % M);
}
