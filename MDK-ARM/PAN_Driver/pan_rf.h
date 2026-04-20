/*******************************************************************************
 * @note Copyright (C) 2023 Shanghai Panchip Microelectronics Co., Ltd. All rights reserved.
 *
 * @file pan_rf.h
 * @brief
 *
 * @history - V0.8, 2024-4
*******************************************************************************/
#ifndef __PAN_RF_H
#define __PAN_RF_H

#include "pan_port.h"

//EBIT_LC
/* 其他头文件引用 */
#include "e290_hal.h"

/* result */
typedef enum
{
    RF_OK,    // Operate ok    ==0
    RF_FAIL,  // Operate fail  ==1
    OK = 0,   // Operate ok    ==0
    FAIL,     // Operate fail  ==1
} RF_Err_t;

#define RF_ASSERT(fn)        \
    do                       \
    {                        \
        if (RF_OK != fn)     \
        {                    \
            return RF_FAIL;  \
        }                    \
    } while (0);

#define BIT0           0x01
#define BIT1           0x02
#define BIT2           0x04
#define BIT3           0x08
#define BIT4           0x10
#define BIT5           0x20
#define BIT6           0x40
#define BIT7           0x80

#define BIT(v)         ((u8)(1 << v))                                   // mask for bit v
#define GENMASK(h, l)  ((u8)(((1 << (h + 1)) - 1) & (~((1 << l) - 1)))) // mask for bits[h,l]
#define GET_SHIFT(v)   ((__ctz(v)))                                     // bit shift in a uint8_t value
#define NOBITS         ((u8)0x00)

/* RF mode define*/
#define RF_MODE_DEEP_SLEEP              0
#define RF_MODE_SLEEP                   1
#define RF_MODE_STB1                    2
#define RF_MODE_STB2                    3
#define RF_MODE_STB3                    4
#define RF_MODE_TX                      5
#define RF_MODE_RX                      6

/* RF Tx mode */
#define RF_TX_SINGLE                    0
#define RF_TX_CONTINOUS                 1

/* RF Rx mode */
#define RF_RX_SINGLE                    0
#define RF_RX_SINGLE_TIMEOUT            1
#define RF_RX_CONTINOUS                 2

/* RF power maximum ramp   RF 功率最大斜坡*/
#define RF_MAX_RAMP                     22
#define RF_MIN_RAMP                     1

/* System control register */
#define REG_SYS_CTL                     0x00
#define REG_FIFO_ACC_ADDR               0x01

/* 3V Logical area register */
#define REG_OP_MODE                     0x02

/* dcdc calibration select */
#define CALIBR_REF_CMP  0x01
#define CALIBR_ZERO_CMP 0x02
#define CALIBR_IMAX_CMP 0x03

#define MODEM_MODE_NORMAL               0x01
#define MODEM_MODE_MULTI_SECTOR         0x02

#define MODEM_MPA                       0x01
#define MODEM_MPB                       0x02

#define freq_360000000                  360000000
#define freq_370000000                  370000000
#define freq_385000000                  385000000
#define freq_405000000                  405000000
#define freq_415000000                  415000000
#define freq_430000000                  430000000
#define freq_445000000                  445000000
#define freq_465000000                  465000000
#define freq_485000000                  485000000
#define freq_495000000                  495000000
#define freq_505000000                  505000000
#define freq_530000000                  530000000
#define freq_565000000                  565000000
#define freq_600000000                  600000000

#define freq_720000000                  720000000
#define freq_740000000                  740000000
#define freq_770000000                  770000000
#define freq_810000000                  810000000
#define freq_830000000                  830000000
#define freq_860000000                  860000000
#define freq_890000000                  890000000
#define freq_930000000                  930000000
#define freq_970000000                  970000000
#define freq_1010000000                 1010000000
#define freq_1060000000                 1060000000
#define freq_1080000000                 1080000000

#define CODE_RATE_45                    0x01
#define CODE_RATE_46                    0x02
#define CODE_RATE_47                    0x03
#define CODE_RATE_48                    0x04

#define SF_5                            5
#define SF_6                            6
#define SF_7                            7
#define SF_8                            8
#define SF_9                            9
#define SF_10                           10
#define SF_11                           11
#define SF_12                           12

#define BW_62_5K                        6
#define BW_125K                         7
#define BW_250K                         8
#define BW_500K                         9

#define CRC_OFF                         0
#define CRC_ON                          1

#define PLHD_IRQ_OFF                    0
#define PLHD_IRQ_ON                     1

#define PLHD_OFF                        0
#define PLHD_ON                         1

#define PLHD_LEN8                       0
#define PLHD_LEN16                      1

#define AGC_ON                          1
#define AGC_OFF                         0

#define LO_400M                         0
#define LO_800M                         1

#define DCDC_OFF                        0
#define DCDC_ON                         1

#define LDR_OFF                         0
#define LDR_ON                          1

#define MAPM_OFF                        0
#define MAPM_ON                         1

#define CAD_DETECT_THRESHOLD_0A         0x0A
#define CAD_DETECT_THRESHOLD_10         0x10
#define CAD_DETECT_THRESHOLD_15         0x15
#define CAD_DETECT_THRESHOLD_20         0x20

#define CAD_DETECT_NUMBER_1             0x01
#define CAD_DETECT_NUMBER_2             0x02
#define CAD_DETECT_NUMBER_3             0x03

#define REG_PAYLOAD_LEN                 0x0C

/*IRQ BIT MASK*/
#define REG_IRQ_MAPM_DONE               0x40
#define REG_IRQ_RX_PLHD_DONE            0x10
#define REG_IRQ_RX_DONE                 0x08
#define REG_IRQ_CRC_ERR                 0x04
#define REG_IRQ_RX_TIMEOUT              0x02
#define REG_IRQ_TX_DONE                 0x01

//EBIT_LC_ZY
//我改这看看有没有问题
/* PAN3060最大发射功率 */
#define DEFAULT_PWR                     30

//EBIT_LC_ZY
/* PAN3060默认频率 */
#define DEFAULT_FREQ                    (470114814) //470MHz
#define DEFAULT_SF                      SF_7
#define DEFAULT_BW                      BW_250K
#define DEFAULT_CR                      CODE_RATE_46
         
//EBIT_LC
#define RADIO_FLAG_IDLE                 0
#define RADIO_FLAG_TXDONE               1
#define RADIO_FLAG_RXDONE               2
#define RADIO_FLAG_RXTIMEOUT            3
#define RADIO_FLAG_RXERR                4
#define RADIO_FLAG_PLHDRXDONE           5
#define RADIO_FLAG_MAPM                 6
#define LEVEL_INACTIVE                  0
#define LEVEL_ACTIVE                    1
         
//EBIT_LC
/* 管脚定义 */
#ifdef HARDWARE_V03
#define	GPIO_PIN_IRQ                   E290_IRQ_Pin
#define	GPIO_PORT_IRQ                   E290_IRQ_GPIO_Port
#define	GPIO_PIN_CAD                   E290_GPIO11_Pin
#define	GPIO_PORT_CAD                   E290_GPIO11_GPIO_Port
#endif

#ifdef HARDWARE_DEMO
#define	GPIO_PIN_IRQ                   E290_IRQ_DEMO_Pin
#define	GPIO_PORT_IRQ                   E290_IRQ_DEMO_GPIO_Port
#define	GPIO_PIN_CAD                   E290_GPIO11_DEMO_Pin
#define	GPIO_PORT_CAD                   E290_GPIO11_DEMO_GPIO_Port
#endif

//EBIT_LC
#define CHECK_IRQ()                     PORT_GetBit(GPIO_PORT_IRQ, GPIO_PIN_IRQ)
#define CHECK_CAD()                     PORT_GetBit(GPIO_PORT_CAD, GPIO_PIN_CAD)
#define SET_TIMER_MS(time)              timer6_open_ms(time)
              
#define TEST_MODE_BUFFER_LEN            10

enum REF_CLK_SEL {REF_CLK_32M,REF_CLK_16M};
enum PAGE_SEL {PAGE0_SEL,PAGE1_SEL,PAGE2_SEL, PAGE3_SEL};
enum MAPM_LASTADDR_FUNC {ORDINARY_ADDR,FIELD_COUNTER};

#pragma pack(1)
typedef struct
{
    enum PAGE_SEL page;
    uint8_t addr;
    uint8_t value;
}pan_reg_cfg_t;

struct RxDoneMsg
{
    uint8_t Payload[255];//有效载荷
    uint8_t PlhdPayload[16];//接收提前中断功能
	uint8_t TestModePayload[TEST_MODE_BUFFER_LEN];
    uint16_t PlhdSize;//接收提前中断大小
    uint16_t Size;
    uint16_t mpam_recv_index;
    uint8_t mpam_recv_buf[1024];  //set buf size based on actual application 根据实际应用设置buf大小
    int8_t Rssi;
    float Snr;
};//RxDoneMsg.Size
#pragma pack ()

typedef struct
{
    uint8_t mapm_addr[4];
    uint8_t fn;
    uint8_t fnm;
    uint8_t gfs;
    uint8_t gn;
    uint8_t pg1;
    uint8_t pgn;
    uint16_t pn;
} stc_mapm_cfg_t;

#pragma pack(1)
typedef struct
{
    uint8_t ramp;
    uint8_t pa_trim;
    uint8_t pa_ldo;
    uint8_t pa_duty;
} power_ramp_t;
#pragma pack ()

#pragma pack(1)
typedef struct
{
    uint8_t  ramp;
    uint8_t  pa_ldo;
    uint8_t  pa_duty;
} power_ramp_cfg_t;
#pragma pack ()

uint8_t rf_read_reg(uint8_t addr);
uint8_t rf_write_reg(uint8_t addr, uint8_t value);
void rf_write_fifo(uint8_t addr, uint8_t *buffer, int size);
void rf_read_fifo(uint8_t addr, uint8_t *buffer, int size);
RF_Err_t rf_switch_page(enum PAGE_SEL page);
uint8_t rf_read_spec_page_reg(enum PAGE_SEL page, uint8_t addr);
RF_Err_t rf_read_spec_page_regs(enum PAGE_SEL page, uint8_t addr, uint8_t *buffer, uint8_t len);
RF_Err_t rf_write_spec_page_reg(enum PAGE_SEL page, uint8_t addr, uint8_t value);
RF_Err_t rf_write_spec_page_regs(enum PAGE_SEL page, uint8_t addr, uint8_t *buffer, uint8_t len);
uint8_t rf_clr_irq(uint8_t flags);
uint8_t rf_get_irq(void);
RF_Err_t rf_refresh(void);
uint16_t rf_read_pkt_cnt(void);
void rf_clr_pkt_cnt(void);
RF_Err_t rf_agc_enable(bool NewState);
RF_Err_t rf_set_agc(bool NewState);
RF_Err_t rf_ft_calibr(void);
RF_Err_t rf_reg_cfg(void);
RF_Err_t rf_init(void);
RF_Err_t rf_sleep_wakeup(void);
RF_Err_t rf_deepsleep(void);
RF_Err_t rf_sleep(void);
RF_Err_t rf_set_lo_freq(uint32_t lo);
RF_Err_t rf_set_freq(uint32_t freq);
uint32_t rf_read_freq(void);
uint32_t rf_get_tx_time(uint8_t size);
RF_Err_t rf_set_bw(uint8_t bw_val);
uint8_t rf_get_bw(void);
RF_Err_t rf_set_sf(uint8_t sf_val);
uint8_t rf_get_sf(void);
RF_Err_t rf_set_crc(bool NewState);
uint8_t rf_get_crc(void);
RF_Err_t rf_set_code_rate(uint8_t code_rate);
uint8_t rf_get_code_rate(void);
RF_Err_t rf_set_mode(uint8_t mode);
uint8_t rf_get_mode(void);
RF_Err_t rf_set_tx_mode(uint8_t mode);
RF_Err_t rf_set_rx_mode(uint8_t mode);
RF_Err_t rf_set_modem_mode(uint8_t mode);
RF_Err_t rf_set_rx_single_timeout(uint16_t timeout);
float rf_get_snr(void);
int8_t rf_get_rssi(void);
int8_t rf_get_channel_rssi(void);
RF_Err_t rf_set_tx_power(uint8_t tx_power);
uint8_t rf_get_tx_power(void);
RF_Err_t rf_set_preamble(uint16_t reg);
uint16_t rf_get_preamble(void);
RF_Err_t rf_set_gpio_input(uint8_t gpio_pin);
RF_Err_t rf_set_gpio_output(uint8_t gpio_pin);
RF_Err_t rf_set_gpio_state(uint8_t gpio_pin, uint8_t state);
bool rf_get_gpio_state(uint8_t gpio_pin);
RF_Err_t rf_cad_on(uint8_t preamble_threshold,
                   uint8_t signal_power_thred,
                   uint8_t payload_threshold,
                   uint8_t payload_en);
RF_Err_t rf_cad_off(void);
RF_Err_t rf_set_syncword(uint8_t sync);//同步字
uint8_t rf_get_syncword(void);
RF_Err_t rf_send_packet(uint8_t *buff, int len);
uint8_t rf_recv_packet(uint8_t *buff);
RF_Err_t rf_set_early_irq(bool NewState);
bool rf_get_early_irq(void);
RF_Err_t rf_set_plhd(uint8_t addr, uint8_t len);
uint8_t rf_get_plhd_len(void);
RF_Err_t rf_set_plhd_mask(uint8_t plhd_val);
uint8_t rf_get_plhd_mask(void);

RF_Err_t rf_plhd_receive(uint8_t *buf, uint8_t len);
void rf_set_plhd_rx_on(uint8_t addr, uint8_t len);
void rf_set_plhd_rx_off(void);
RF_Err_t rf_set_dcdc_mode(uint8_t dcdc_val);
RF_Err_t rf_set_ldr(uint32_t mode);
bool rf_get_ldr(void);
int calculate_chirp_count(int sf_range[], int size, int chirp_counts[]);
RF_Err_t rf_set_auto_sf_tx_preamble(int sf, int sf_range[], int size, int chirp_counts[]);
RF_Err_t rf_set_auto_sf_rx_on(int sf_range[], int size);
RF_Err_t rf_set_auto_sf_rx_off(void);
RF_Err_t rf_set_carrier_wave_on(void);
RF_Err_t rf_set_carrier_wave_freq(uint32_t freq);
RF_Err_t rf_set_carrier_wave_off(void);
RF_Err_t carrier_wave_test_mode(void);
RF_Err_t rf_mapm_en(void);
RF_Err_t rf_mapm_dis(void);
RF_Err_t rf_set_mapm_mask(uint8_t mapm_val);
RF_Err_t rf_set_mapm_para(uint8_t field_num_mux, uint8_t group_fun_sel, uint8_t gn, uint8_t pgn);
RF_Err_t rf_set_mapm_addr(uint8_t addr_no, uint8_t addr);
uint32_t rf_calculate_mapm_preambletime(stc_mapm_cfg_t *mapm_cfg, uint32_t one_chirp_time);
uint8_t rf_get_mapm_field_num(void);
RF_Err_t rf_set_mapm_field_num(uint8_t);
RF_Err_t rf_set_mapm_field_num_mux(uint8_t fnm);
RF_Err_t rf_set_mapm_group_fun_sel(uint8_t gfs);
RF_Err_t rf_set_mapm_group_num(uint8_t gn);
RF_Err_t rf_set_mapm_firgroup_preamble_num(uint8_t pgl);
RF_Err_t rf_set_mapm_group_preamble_num(uint8_t pgn);
RF_Err_t rf_set_mapm_neces_preamble_num(uint16_t pn);
void rf_set_mapm_on(void);
void rf_set_mapm_off(void);
void rf_set_mapm_cfg(stc_mapm_cfg_t *p_mapm_cfg);
RF_Err_t rf_efuse_on(void);
RF_Err_t rf_efuse_off(void);
uint8_t rf_efuse_read_encry_byte(uint8_t reg_addr, uint16_t pattern, uint8_t efuse_addr);
uint8_t rf_efuse_read_byte(uint8_t reg_addr, uint8_t efuse_addr);
void rf_efuse_write_encry_byte(uint8_t reg_addr, uint16_t pattern, uint8_t efuse_addr, uint8_t value);
void rf_efuse_write_byte(uint8_t reg_addr, uint8_t efuse_addr, uint8_t value);
RF_Err_t rf_set_dcdc_calibr_on(uint8_t calibr_type);
RF_Err_t rf_set_dcdc_calibr_off(void);
RF_Err_t rf_set_ldo_pa_on(void);
RF_Err_t rf_set_ldo_pa_off(void);
RF_Err_t rf_enter_continous_rx(void);
RF_Err_t rf_enter_single_timeout_rx(uint32_t timeout);
RF_Err_t rf_enter_single_rx(void);
RF_Err_t rf_single_tx_data(uint8_t *buf, uint8_t size, uint32_t *tx_time);
RF_Err_t rf_enter_continous_tx(void);
RF_Err_t rf_continous_tx_send_data(uint8_t *buf, uint8_t size);
void rf_irq_process(void);
uint32_t rf_get_chirp_time(uint8_t bw, uint8_t sf);
bool check_cad_rx_inactive(uint32_t one_chirp_time);
RF_Err_t rf_set_default_para(void);

int rf_get_recv_flag(void);
void rf_set_recv_flag(int status);
int rf_get_transmit_flag(void);
void rf_set_transmit_flag(int status);

#endif
