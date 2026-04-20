/*******************************************************************************
 * @note Copyright (C) 2023 Shanghai Panchip Microelectronics Co., Ltd. All rights reserved.
 *
 * @file pan_rf.c
 * @brief
 *
 * @history - V0.8, 2024-4
*******************************************************************************/
#include "pan_port.h"
#include "pan_param.h"
#include "spi.h"
#include "rs485bsp.h"
#include "app_st25dv.h"
/*
 * flag that indicate if a new packet is received.
 * 指示是否收到新数据包的标志。
*/
int packet_received = RADIO_FLAG_IDLE;

/*
 * flag that indicate if transmision is finished.
 * 指示传输是否完成的标志。
*/
static int packet_transmit = RADIO_FLAG_IDLE;

struct RxDoneMsg RxDoneParams;

/**
 * @brief get receive flag 获取接收标志
 * @param[in] <none>
 * @return receive state 接收状态
 */
int rf_get_recv_flag(void)
{
    return packet_received;
}

/**
 * @brief set receive flag 设置接收标志
 * @param[in] <status> receive flag state to set 接收标志状态设置
 * @return none
 */
void rf_set_recv_flag(int status)
{
    packet_received = status;
}

/**
 * @brief get transmit flag 获取发射状态标志
 * @param[in] <none>
 * @return reansmit state
 */
int rf_get_transmit_flag(void)
{
    return packet_transmit;
}

/**
 * @brief set transmit flag 设置发送状态标志
 * @param[in] <status> transmit flag state to set 发送标志状态设置
 * @return none
 */
void rf_set_transmit_flag(int status)
{
    packet_transmit = status;
}

/**
 * @brief count trailing zeros 计算尾随零的数量
 *
 * @param val
 * @return int
 */
static uint8_t __ctz(uint8_t val)
{
    int i;

    for (i = 0; i < 8; ++i)
    {
        if ((val >> i) & 1)
            return (uint8_t)i;
    }
    
    return 0;
}

/**
 * @brief read one byte from register in current page
 * @param[in] <addr> register address to write
 * @return value read from register
* @brief 从当前页的寄存器读取一个字节
 * @param[in] <addr> 要写入的寄存器地址
 * @return 从寄存器读取的值
 */
uint8_t rf_read_reg(uint8_t addr)//0x04
{
    uint8_t txbuf[2],rxbuf[2];//临时寄存器
		txbuf[0]=addr<<1;
    rf_port.spi_cs_low();
		HAL_SPI_TransmitReceive(&hspi1 ,txbuf, rxbuf,2, 1000);
    rf_port.spi_cs_high();

    return rxbuf[1];
}

/**
 * @brief write global register in current page and chick
 * @param[in] <addr> register address to write
 * @param[in] <value> address value to write to rgister
 * @return result
 */
uint8_t rf_write_reg(uint8_t addr, uint8_t value)
{
    uint8_t txbuf[2],rxbuf[2];//临时寄存器
		txbuf[0]=(0x01 | (addr << 1));
		txbuf[1]=value;
    rf_port.spi_cs_low();
		HAL_SPI_TransmitReceive(&hspi1 ,txbuf, rxbuf,2, 1000);
    rf_port.spi_cs_high();
	
#if SPI_WRITE_CHECK
    if (rf_read_reg(addr) != value)
    {
        return FAIL;
    }
#endif
    return OK;
}

/**
 * @brief rf send data fifo,send bytes register
 * @param[in] <addr> register address to write
 * @param[in] <buffer> send data buffer
 * @param[in] <size> send data size
 * @return none
 */
void rf_write_fifo(uint8_t addr, uint8_t *buffer, int size)
{
    int i;
    uint8_t addr_w = (0x01 | (addr << 1));

    rf_port.spi_cs_low();
    rf_port.spi_readwrite(addr_w);
    for (i = 0; i < size; i++)
    {
        rf_port.spi_readwrite(buffer[i]);
    }
    rf_port.spi_cs_high();
}

/**
 * @brief rf receive data fifo,read bytes from register
 * @param[in] <addr> register address to write
 * @param[in] <buffer> receive data buffer
 * @param[in] <size> receive data size
 * @return none
 */
void rf_read_fifo(uint8_t addr, uint8_t *buffer, int size)
{
    int i;
    uint8_t addr_w = (0x00 | (addr << 1));

    rf_port.spi_cs_low();
    rf_port.spi_readwrite(addr_w);
    for (i = 0; i < size; i++)
    {
        buffer[i] = rf_port.spi_readwrite(0x00);
    }
    rf_port.spi_cs_high();
}

/**
 * @brief switch page
 * @param[in] <page> page to switch
 * @return result
 */
RF_Err_t rf_switch_page(enum PAGE_SEL page)
{
    uint8_t page_sel;
    uint8_t tmpreg;

    tmpreg = rf_read_reg(REG_SYS_CTL);
    page_sel = (tmpreg & 0xFC) | page;
    rf_write_reg(REG_SYS_CTL, page_sel);

#if SPI_WRITE_CHECK
    if ((rf_read_reg(REG_SYS_CTL) & 0x03) != page)
    {
        return FAIL;
    }
#endif
    
    return OK;
}

/**
 * @brief This function write a value to register in specific page
 * @brief 此函数将值写入特定页面的寄存器中
 * @param[in] <page> the page of register
 * @param[in] <addr> register address
 * @param[in] <value> value to write
 * @return result
 */
RF_Err_t rf_write_spec_page_reg(enum PAGE_SEL page, uint8_t addr, uint8_t value)
{
    RF_ASSERT(rf_switch_page(page));
    RF_ASSERT(rf_write_reg(addr, value));

    return OK;
}

/**
 * @brief read a value to register in specific page
 * @param[in] <page> the page of register
 * @param[in] <addr> register address
 * @return success(register value) or failure
 */
uint8_t rf_read_spec_page_reg(enum PAGE_SEL page, uint8_t addr)
{
    RF_ASSERT(rf_switch_page(page));

    return rf_read_reg(addr);
}

/**
 * @brief write continue register valuies(buffer) in specific addr page
 * @brief 在特定地址页中写入继续寄存器值（缓冲区）
 * @param[in] <page> the page of register
 * @param[in] <addr> register start address
 * @param[in] <buffer> values to write
 * @param[in] <len> buffer len
 * @return result
 */
RF_Err_t rf_write_spec_page_regs(enum PAGE_SEL page, uint8_t addr, uint8_t *buffer, uint8_t len)
{
    uint8_t i;
    uint16_t addr_w;
    RF_Err_t ret = RF_OK;

    RF_ASSERT(rf_switch_page(page));

    addr_w = (0x01 | (addr << 1));
    rf_port.spi_cs_low();
    rf_port.spi_readwrite(addr_w);
    for (i = 0; i < len; i++)
    {
        rf_port.spi_readwrite(buffer[i]);
    }
    rf_port.spi_cs_high();

#if SPI_WRITE_CHECK
    rf_port.spi_cs_low();
    rf_port.spi_readwrite(0x00 | (addr << 1));
    for (i = 0; i < len; i++)
    {
        if(buffer[i] != rf_port.spi_readwrite(0x00))
        {
            ret = FAIL;
            break;
        }
    }
    rf_port.spi_cs_high();
#endif

    return ret;
}

RF_Err_t rf_read_spec_page_regs(enum PAGE_SEL page, uint8_t addr, uint8_t *buffer, uint8_t len)
{
    uint8_t i;
    
    RF_ASSERT(rf_switch_page(page));

    rf_port.spi_cs_low();
    rf_port.spi_readwrite(0x00 | (addr << 1));
    for (i = 0; i < len; i++)
    {
        buffer[i] = rf_port.spi_readwrite(0x00);
    }
    rf_port.spi_cs_high();

    return OK;
}

/**
 * @brief Set bits to 1
 *
 * @param page
 * @param addr
 * @param bit_mask
 * @return RF_Err_t
 */
RF_Err_t rf_set_spec_page_reg_bits(enum PAGE_SEL page, uint8_t addr, uint8_t mask)
{
    uint8_t tmp;
    RF_Err_t ret;

    tmp = rf_read_spec_page_reg(page, addr);
    ret = rf_write_spec_page_reg(page, addr, tmp | mask);

    return ret;
}

/**
 * @brief Set bits to 0
 *
 * @param page
 * @param addr
 * @param bit_mask
 * @return RF_Err_t
 */
RF_Err_t rf_reset_spec_page_reg_bits(enum PAGE_SEL page, uint8_t addr, uint8_t mask)
{
    uint8_t tmp;
    RF_Err_t ret;
    
    tmp = rf_read_spec_page_reg(page, addr);
    ret = rf_write_spec_page_reg(page, addr, tmp & (~mask));
    
    return ret;
}

RF_Err_t rf_write_spec_page_reg_bits(enum PAGE_SEL page, uint8_t addr, uint8_t val, uint8_t mask)
{
    uint8_t tmp;
    RF_Err_t ret;
    uint8_t shift = GET_SHIFT(mask);

    val <<= shift;
    val &= mask;

    tmp = rf_read_spec_page_reg(page, addr);
    ret = rf_write_spec_page_reg(page, addr, (tmp & (~mask)) | val);

    return ret;
}

/**
 * @brief rf clear all irq
 * @param[in] <none>
 * @return result
 */
uint8_t rf_clr_irq(uint8_t flags)
{
    rf_write_spec_page_reg(PAGE0_SEL, 0x6C, flags); // clr irq
	
    return OK;
}

/**
 * @brief get irq status
 * @param[in] <none>
 * @return ira status
 */
uint8_t rf_get_irq(void)
{
    uint8_t tmpreg = rf_read_spec_page_reg(PAGE0_SEL, 0x6C);

    return (tmpreg & 0x7F);
}

/**
 * @brief RF 1.2V register refresh, Will not change register values
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_refresh(void)
{
    uint8_t tmpreg;

    tmpreg = rf_read_reg(REG_SYS_CTL);
    tmpreg |= 0x80;
    rf_write_reg(REG_SYS_CTL, tmpreg);

    tmpreg = rf_read_reg(REG_SYS_CTL);
    tmpreg &= 0x7F;
    rf_write_reg(REG_SYS_CTL, tmpreg);

    rf_read_reg(REG_SYS_CTL);

    return OK;
}

/**
 * @brief read packet count register
 * @param[in] <none>
 * @return packet count
 */
uint16_t rf_read_pkt_cnt(void)
{
    uint8_t reg_low, reg_high;
    uint16_t pkt_cnt;

    reg_low = rf_read_spec_page_reg(PAGE1_SEL, 0x6c);
    reg_high = rf_read_spec_page_reg(PAGE1_SEL, 0x6d);

    pkt_cnt = (reg_high << 8) | reg_low;

    return pkt_cnt;
}

/**
 * @brief clear packet count register
 * @param[in] <none>
 * @return none
 */
void rf_clr_pkt_cnt(void)
{
    uint8_t tmpreg;

    tmpreg = rf_read_reg(REG_SYS_CTL);
    tmpreg |= 0x40;
    rf_write_reg(REG_SYS_CTL, tmpreg);

//    tmpreg = rf_read_reg(REG_SYS_CTL);
    tmpreg = (tmpreg & 0xbf);
    rf_write_reg(REG_SYS_CTL, tmpreg);
}

/**
 * @brief enable AGC function
 * @param[in] <state>
 *			  AGC_OFF/AGC_ON
 * @return result
 */
RF_Err_t rf_agc_enable(bool NewState)
{
    if(NewState == AGC_OFF)
    {
        RF_ASSERT(rf_set_spec_page_reg_bits(PAGE2_SEL, 0x06, BIT0));
    }
    else
    {
        RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE2_SEL, 0x06, BIT0));
    }

    return OK;
}

/**
 * @brief configure AGC function
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_set_agc(bool NewState)
{
    RF_ASSERT(rf_agc_enable(NewState));
    // RF_ASSERT(rf_write_spec_page_regs(PAGE2_SEL, 0x0a, (uint8_t *)reg_agc_freq400, 40));
    RF_ASSERT(rf_write_spec_page_reg(PAGE2_SEL, 0x34, 0xef));

    return OK;
}

/**
 * @brief do basic configuration to initialize 进行基本配置以初始化
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_ft_calibr(void)
{
    uint8_t i, tmpreg, cal[0x26] = {0};

    rf_efuse_on();

    for (i = 17; i < 20; i++)
    {
        cal[0x0d + i] = rf_efuse_read_encry_byte(0x3b, 0x5aa5, 0x0d + i);
    }

    if (rf_efuse_read_encry_byte(0x3b, 0x5aa5, 0x1c) == 0x5a)
    {
        rf_write_spec_page_reg(PAGE2_SEL, 0x3d, 0xfd);
        
        if (cal[0x0d + 19] != 0)
        {
            rf_write_spec_page_reg(PAGE0_SEL, 0x45, cal[0x0d + 19]);
        }

        if (rf_efuse_read_encry_byte(0x3b, 0x5aa5, 0x0d) == MODEM_MPA)
        {
            RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x1C, cal[0x1E]&0x1F, 0x1F));
        }
        else if (rf_efuse_read_encry_byte(0x3b, 0x5aa5, 0x0d) == MODEM_MPB)
        {
            tmpreg = (0xc0 | (cal[0x1e] & 0x1f));
            rf_write_spec_page_reg(PAGE3_SEL, 0x1c, tmpreg);
        }

        rf_write_spec_page_reg(PAGE3_SEL, 0x1d, cal[0x1f]);
    }

    rf_efuse_off();

    return OK;
}

/**
 * @brief rf register configuration
 * @param[in] <none>
 * @return none
 */
RF_Err_t rf_reg_cfg(void)
{
    for(int i = 0; i < sizeof(g_reg_cfg)/sizeof(pan_reg_cfg_t); i++)
    {
        RF_ASSERT(rf_write_spec_page_reg(g_reg_cfg[i].page, g_reg_cfg[i].addr, g_reg_cfg[i].value));
    }

    return OK;
}

/**
 * @brief change rf mode from deep sleep to standby3(STB3)
 * 将射频模式从深度睡眠模式更改为待机模式3(STB3)
 * @param[in] <none>
 * @return result
 */


RF_Err_t rf_init(void)
{
    uint8_t rstreg, porreg;

    porreg = rf_read_reg(0x04);
    porreg |= 0x10;
    rf_write_reg(0x04, porreg);
    
    rf_port.delayus(10);
    
    porreg &= 0xEF;
    rf_write_reg(0x04, porreg);
    rstreg = rf_read_reg(REG_SYS_CTL);
    rstreg &= 0x7F;
    rf_write_reg(REG_SYS_CTL, rstreg);
    rf_port.delayus(10);
    rstreg |= 0x80;
    rf_write_reg(REG_SYS_CTL, rstreg);
    rf_port.delayus(10);
    rstreg &= 0x7F;
    rf_write_reg(REG_SYS_CTL, rstreg);
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_DEEP_SLEEP));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_SLEEP));
    rf_port.delayus(10);

    RF_ASSERT(rf_set_spec_page_reg_bits(PAGE3_SEL, 0x06, BIT5));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB1));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, 0x26, 0x2f));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(0x04, 0x36));
    rf_port.delayus(10);

    rf_port.tcxo_init();

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB2));
    rf_port.delayus(150);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB3));
    rf_port.delayus(10);

    RF_ASSERT(rf_ft_calibr());
    RF_ASSERT(rf_reg_cfg());
    RF_ASSERT(rf_set_agc(AGC_ON));

    rf_port.antenna_init();

    return OK;
}

/**
 * @brief change rf mode from sleep to standby3(STB3)
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_sleep_wakeup(void)
{
    // RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_SLEEP));

    // rf_port.delayus(10);

    RF_ASSERT(rf_set_spec_page_reg_bits(PAGE3_SEL, 0x06, BIT5));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB1));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, 0x26, 0x2f));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(0x04, 0x36));
    rf_port.delayus(10);

    rf_port.tcxo_init();

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB2));
    rf_port.delayus(150);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB3));

    rf_port.delayus(10);
    rf_port.antenna_init();

    return OK;
}

/**
 * @brief change rf mode from standby3(STB3) to deep sleep, rf should set DCDC_OFF before enter deepsleep
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_deepsleep(void)
{
	rf_port.antenna_close();
    rf_port.delayus(10);
    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB3));
    rf_port.delayus(150);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB2));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB1));
    rf_port.delayus(10);

    rf_port.tcxo_close();

    RF_ASSERT(rf_write_reg(0x04, 0x06));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_SLEEP));
    rf_port.delayus(10);

    RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE3_SEL, 0x06, BIT5));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, 0x26, 0x0f));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_DEEP_SLEEP));

    return OK;
}

/**
 * @brief change rf mode from standby3(STB3) to sleep, rf should set DCDC_OFF before enter sleep
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_sleep(void)
{
	rf_port.antenna_close();

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB3));
    rf_port.delayus(150);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB2));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB1));

    rf_port.delayus(10);
    rf_port.tcxo_close();

    RF_ASSERT(rf_write_reg(0x04, 0x16));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_SLEEP));

    RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE3_SEL, 0x06, BIT5));
    rf_port.delayus(10);

    RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, 0x26, 0x0f));

    return OK;
}

/**
 * @brief set LO frequency
 * @param[in] <lo> LO frequency
 *			  LO_400M / LO_800M
 * @return result
 */
RF_Err_t rf_set_lo_freq(uint32_t lo)
{
    if(lo == LO_400M)
    {
        RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x3D, 1, BIT6|BIT5|BIT4));
    }
    else // if(lo == LO_800M)
    {
        RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x3D, 0, BIT6|BIT5|BIT4));
    }

    return OK;
}




RF_Err_t rf_set_freq(uint32_t freq)//射频频率设置,470MHz
{
    uint8_t lowband_sel = 0; // 400M 800M is same value?  低频带
    float tmp_var;//tmp:临时  var:变量
    int integer_part;//整数部分
    float fractional_part;//分数部分
    
    int fb, fc;

    if(freq < freq_405000000 || freq > freq_1080000000)//405MHz~1080MHz
    {
        return FAIL;
    }
    
    
    

    if (freq < 800000000)//小于800MHz
    {
        RF_ASSERT(rf_write_spec_page_regs(PAGE2_SEL, 0x0A, (uint8_t *)reg_agc_freq400, 40));
    }
    else
    {
        RF_ASSERT(rf_write_spec_page_regs(PAGE2_SEL, 0x0A, (uint8_t *)reg_agc_freq800, 40));
    }

    
    
    
    for(int i = 0; i < 16; i++)//循环16次
    {
        if(freq == freq_405000000 || freq == freq_810000000)
        {
            RF_ASSERT(rf_write_spec_page_reg(PAGE0_SEL, 0x40, 0x1A));
            break;
        }
        else if(freq > freq_table[i][0] && freq <= freq_table[i][1])
        {
            RF_ASSERT(rf_write_spec_page_reg(PAGE0_SEL, 0x40, freq_param_table[i]));
            break;
        }
    }

    
    
    
    if (freq < 800000000)
    {
        tmp_var = freq * 4.0 / 32000000;
        rf_set_lo_freq(LO_400M);
    }
    else
    {
        tmp_var = freq * 2.0 / 32000000;
        rf_set_lo_freq(LO_800M);
    }

    
    
    
    integer_part = (int)tmp_var; // keep the integer part
    fractional_part = tmp_var - integer_part; // keep the fractional part

    
    
    fb = integer_part - 20;
    fc = (int)(fractional_part * 1600 / (2 * (1 + lowband_sel)));

    uint8_t temp_fx[3] = {fb & 0xff, fc & 0xff, (fc >> 8) & 0x0f};
    RF_ASSERT(rf_write_spec_page_regs(PAGE3_SEL, 0x15, temp_fx, 3));

    uint8_t temp_freq[4] = {freq & 0xff, (freq >> 8) & 0xff, (freq >> 16) & 0xff, (freq >> 24) & 0xff};
    RF_ASSERT(rf_write_spec_page_regs(PAGE3_SEL, 0x09, temp_freq, 4));
        
        

    return OK;
}

/**
 * @brief read frequency(in Hz)
 * @param[in] <none>
 * @return frequency(in Hz)
 */
uint32_t rf_read_freq(void)
{
    uint32_t freq;
    uint8_t temp[4];

    rf_read_spec_page_regs(PAGE3_SEL, 0x09, temp, 4);
    freq = ((uint32_t)temp[3] << 24) | ((uint32_t)temp[2] << 16) | ((uint32_t)temp[1] << 8) | (uint32_t)temp[0];

    return freq;
}

/**
 * @brief calculate tx time
 * @param[in] <size> tx len
 * @return tx time(us)
 */
uint32_t rf_get_tx_time(uint8_t size)
{
    uint8_t sf = rf_get_sf();
    uint8_t cr = rf_get_code_rate();
    uint8_t bw = rf_get_bw();
    uint8_t ldr = rf_get_ldr();
    uint32_t preamble = rf_get_preamble();

    const float bw_table[4] = {62.5, 125, 250, 500};

    if (bw < BW_62_5K || bw > BW_500K)
    {
        return 0;
    }

    float symbol_len = (float)(1 << sf) / bw_table[bw - BW_62_5K]; // symbol length
    float preamble_time;                                // preamble time:ms
    float payload_time = 0;                             // payload time:ms
    float total_time;                                   // total time:ms

    if (sf < 7)
    {
        preamble_time = (preamble + 6.25f) * symbol_len;
        payload_time = ceil((float)(size * 8 - sf * 4 + 36) / ((sf - ldr * 2) * 4));
    }
    else
    {
        preamble_time = (preamble + 4.25f) * symbol_len;
        payload_time = ceil((float)(size * 8 - sf * 4 + 44) / ((sf - ldr * 2) * 4));
    }

    payload_time = payload_time * (cr + 4);
    payload_time = payload_time + 8;
    payload_time = payload_time * symbol_len;
    total_time = (preamble_time + payload_time) * 1000;

    return (uint32_t)total_time;
}

/**
 * @brief check if ldr should be turned on
 * 
 * @param sf 
 * @param bw 
 * @return true 
 * @return false 
 */
bool rf_should_turnon_ldr(uint8_t sf, uint8_t bw) 
{
    if (sf == SF_11)
    {
        if (bw == BW_62_5K || bw == BW_125K)
        {
            return true;
        }
    }
    else if (sf == SF_12)
    {
        if (bw == BW_62_5K || bw == BW_125K || bw == BW_250K)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief set bandwidth
 * @param[in] <bw_val> value relate to bandwidth
 *			  BW_62_5K / BW_125K / BW_250K / BW_500K
 * @return result
 */
RF_Err_t rf_set_bw(uint8_t bw)
{
    uint8_t sf, ldr;

    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x0D, bw, BIT7|BIT6|BIT5|BIT4));

    sf = rf_get_sf();
    
    ldr = rf_should_turnon_ldr(sf, bw);

    rf_set_ldr(ldr);

    if (bw == BW_62_5K || bw == BW_125K || bw == BW_250K)
    {
        RF_ASSERT(rf_set_spec_page_reg_bits(PAGE2_SEL, 0x3F, BIT1));
    }
    else
    {
        RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE2_SEL, 0x3F, BIT1));
    }

    return OK;
}

/**
 * @brief read bandwidth
 * @param[in] <none>
 * @return bandwidth
 */
uint8_t rf_get_bw(void)
{
    uint8_t tmpreg = rf_read_spec_page_reg(PAGE3_SEL, 0x0d);

    return (tmpreg >> 4);
}

/**
 * @brief set spread factor
 * @param[in] <sf> spread factor to set
 *			 SF_5 / SF_6 /SF_7 / SF_8 / SF_9 / SF_10 / SF_11 / SF_12
 * @return result
 */
RF_Err_t rf_set_sf(uint8_t sf)
{
    uint8_t bw, ldr;
	
    if(sf < SF_5 || sf > SF_9)//这款芯片是3060，SF就是5-9；如果芯片是3029，SF就是5-12
    {
        return FAIL;
    }

    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x0E, sf, BIT7|BIT6|BIT5|BIT4));

    bw = rf_get_bw();
    
    ldr = rf_should_turnon_ldr(sf, bw);

    rf_set_ldr(ldr);

    return OK;
}

/**
 * @brief read Spreading Factor
 * @param[in] <none>
 * @return Spreading Factor
 */
uint8_t rf_get_sf(void)
{
    uint8_t tmpreg = rf_read_spec_page_reg(PAGE3_SEL, 0x0E);

    return (tmpreg >> 4);
}

/**
 * @brief set payload CRC
 * @param[in] <NewState> CRC to set
 *			  CRC_ON / CRC_OFF
 * @return result
 */
RF_Err_t rf_set_crc(bool NewState)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x0E, NewState, BIT3));

    return OK;
}

/**
 * @brief read payload CRC
 * @param[in] <none>
 * @return CRC status
 */
uint8_t rf_get_crc(void)
{
    uint8_t tmpreg = rf_read_spec_page_reg(PAGE3_SEL, 0x0e);

    return (tmpreg & 0x08) >> 3;
}

/**
 * @brief set code rate
 * @param[in] <code_rate> code rate to set
 *			  CODE_RATE_45 / CODE_RATE_46 / CODE_RATE_47 / CODE_RATE_48
 * @return result
 */
RF_Err_t rf_set_code_rate(uint8_t code_rate)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x0D, code_rate, BIT3|BIT2|BIT1));

    return OK;
}

/**
 * @brief get code rate
 * @param[in] <none>
 * @return code rate
 */
uint8_t rf_get_code_rate(void)
{
    uint8_t tmpreg;
	uint8_t code_rate;

    tmpreg = rf_read_spec_page_reg(PAGE3_SEL, 0x0D);
    code_rate = ((tmpreg & 0x0E) >> 1); // BIT3|BIT2|BIT1

    return code_rate;
}

/**
 * @brief set rf mode
 * @param[in] <mode>
 *			  RF_MODE_DEEP_SLEEP / RF_MODE_SLEEP
 *			  RF_MODE_STB1 / RF_MODE_STB2
 *			  RF_MODE_STB3 / RF_MODE_TX / RF_MODE_RX
 * @return result
 */
RF_Err_t rf_set_mode(uint8_t mode)
{
    RF_ASSERT(rf_write_reg(REG_OP_MODE, mode));

    return OK;
}

/**
 * @brief get rf mode
 * @param[in] <none>
 * @return mode
 *		   RF_MODE_DEEP_SLEEP / RF_MODE_SLEEP
 *		 RF_MODE_STB1 / RF_MODE_STB2
 *		 RF_MODE_STB3 / RF_MODE_TX / RF_MODE_RX
 */
uint8_t rf_get_mode(void)
{
    return rf_read_reg(REG_OP_MODE);
}

/**
 * @brief set rf Tx mode
 * @param[in] <tx_mode>
 *			  RF_TX_SINGLE/RF_TX_CONTINOUS
 * @return result
 */
RF_Err_t rf_set_tx_mode(uint8_t tx_mode)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x06, tx_mode, BIT2));

    return OK;
}

/**
 * @brief set rf Rx mode
 * @param[in] <mode>
 *			  RF_RX_SINGLE/RF_RX_SINGLE_TIMEOUT/RF_RX_CONTINOUS
 * @return result
 */
RF_Err_t rf_set_rx_mode(uint8_t rx_mode)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x06, rx_mode, BIT1|BIT0));

    return OK;
}

/**
 * @brief set modem mode
 * @param[in] <modem_mode>
 *			  MODEM_MODE_NORMAL / MODEM_MODE_MULTI_SECTOR
 * @return result
 */
RF_Err_t rf_set_modem_mode(uint8_t modem_mode)
{
    if (modem_mode == MODEM_MODE_NORMAL)
    {
        RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x0b, 0x08));
    }
    else if (modem_mode == MODEM_MODE_MULTI_SECTOR)
    {
        RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x0b, 0x18));
        RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x2f, 0x54));
        RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x30, 0x40));
    }

    return OK;
}

/**
 * @brief set timeout for Rx. It is useful in RF_RX_SINGLE_TIMEOUT mode
 * @param[in] <timeout> rx single timeout time(in ms)
 * @return result
 */
RF_Err_t rf_set_rx_single_timeout(uint16_t timeout)
{
    uint8_t temp[2] = {timeout & 0xff, (timeout >> 8) & 0xff};
    RF_ASSERT(rf_write_spec_page_regs(PAGE3_SEL, 0x07, temp, 2));

    return OK;
}

/**
 * @brief get snr value
 * @param[in] <none>
 * @return snr
 */
float rf_get_snr(void)
{
    float snr_val;
    uint32_t sig_pow_val;
    uint32_t noise_pow_val;
    uint32_t sf_val;
    uint8_t temp[3];

    rf_read_spec_page_regs(PAGE2_SEL, 0x71, temp, 3);
    noise_pow_val = ((temp[2] << 16) | (temp[1] << 8) | temp[0]);

    rf_read_spec_page_regs(PAGE1_SEL, 0x74, temp, 3);
    sig_pow_val = ((temp[2] << 16) | (temp[1] << 8) | temp[0]);

    sf_val = (rf_read_spec_page_reg(PAGE1_SEL, 0x7c) & 0xf0) >> 4;

    if (noise_pow_val == 0)
    {
        noise_pow_val = 1;
    }

    snr_val = (float)(10 * log10((sig_pow_val / (2 << sf_val)) / noise_pow_val));

    return snr_val;
}

/**
 * @brief get rssi value
 * @param[in] <none>
 * @return rssi
 */
int8_t rf_get_rssi(void)
{
    return rf_read_spec_page_reg(PAGE1_SEL, 0x7F);
}

/**
 * @brief current channel energy detection
 * @param[in] <none>
 * @return rssi
 */
int8_t rf_get_channel_rssi(void)
{
    int8_t rssi_energy;

    RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE2_SEL, 0x06, BIT2));
    RF_ASSERT(rf_set_spec_page_reg_bits(PAGE2_SEL, 0x06, BIT2));

    rssi_energy = rf_read_spec_page_reg(PAGE1_SEL, 0x7E);

    return rssi_energy;
}

/**
 * @brief set tx_power
 * @param[in] <tx_power> open gears (range in 1--23（405MHz-565MHz）1-22(868/915MHz))
 * @return result
 */

/* 发射功率 */
RF_Err_t rf_set_tx_power(uint8_t tx_power)
{
    uint8_t tmp_value1, tmp_value2, pa_bias;
    uint32_t freq, pwr_table;

    if (tx_power < RF_MIN_RAMP)
    {
        tx_power = RF_MIN_RAMP;//最小为1
    }

    freq = rf_read_freq();

    if ((freq >= freq_405000000) && (freq <= freq_565000000))
    {
        if (tx_power > RF_MAX_RAMP + 1)
        {
            tx_power = RF_MAX_RAMP;//最大为22
        }

        /* modulate wave ramp mode */
        RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x1E, power_ramp_cfg[tx_power - 1].ramp, 0x3F));
        RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x4B, power_ramp_cfg[tx_power - 1].pa_ldo >> 4, 0x0F));
        RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x22, power_ramp_cfg[tx_power - 1].pa_ldo & 0x01, BIT0));

        if (power_ramp_cfg[tx_power - 1].pa_duty != 0x70)
        {
            RF_ASSERT(rf_set_spec_page_reg_bits(PAGE0_SEL, 0x46, BIT2));
        }
        else
        {
            RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE0_SEL, 0x46, BIT2));
        }
        rf_efuse_on();
        pa_bias = rf_efuse_read_encry_byte(0x3b, 0x5aa5, 0x0d + 19);
        rf_efuse_off();
        if (pa_bias == 0)
        {
            pa_bias = 8;
        }

        tmp_value1 = pa_bias - (power_ramp_cfg[tx_power - 1].pa_duty & 0x0f);
        tmp_value2 = (power_ramp_cfg[tx_power - 1].pa_duty & 0xf0) | tmp_value1;
        RF_ASSERT(rf_write_spec_page_reg(PAGE0_SEL, 0x45, tmp_value2));

        return OK;
    }
    else if ((freq >= freq_810000000) && (freq <= freq_890000000))
    {
        pwr_table = 2;
    }
    else if ((freq >= freq_890000000) && (freq <= freq_1080000000))
    {
        pwr_table = 3;
    }
    else
    {
        return FAIL;
    }

    if (tx_power > RF_MAX_RAMP)
    {
        tx_power = RF_MAX_RAMP;
    }

    // modulate wave ramp mode
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x1E, power_ramp[tx_power - 1][pwr_table].ramp, 0x3F));
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x4B, power_ramp[tx_power - 1][pwr_table].pa_trim, 0x0F));
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x22, power_ramp[tx_power - 1][pwr_table].pa_ldo, BIT0));

    if (power_ramp[tx_power - 1][pwr_table].pa_duty != 0xff)
    {
        RF_ASSERT(rf_set_spec_page_reg_bits(PAGE0_SEL, 0x46, BIT2));
        RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x45, power_ramp[tx_power - 1][pwr_table].pa_duty, BIT6|BIT5|BIT4));
    }
    else
    {
        RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE0_SEL, 0x46, BIT2));
    }

    return OK;
}

/**
 * @brief get tx_power
 * @param[in] <none>
 * @return tx_power if return value is 0,means get tx_power fail
 */
uint8_t rf_get_tx_power(void)
{
    uint8_t open_ramp, pa_trim, pa_ldo, pa_duty, pa_duty_en;
    uint8_t i, pa_bias;
    uint32_t freq, pwr_table;

    open_ramp = rf_read_spec_page_reg(PAGE0_SEL, 0x1e) & 0x3f;
    pa_trim = rf_read_spec_page_reg(PAGE0_SEL, 0x4b) & 0x0f;
    pa_ldo = rf_read_spec_page_reg(PAGE3_SEL, 0x22) & 0x01;
    pa_duty = ((rf_read_spec_page_reg(PAGE0_SEL, 0x45) & 0x70) >> 4);
    pa_duty_en = ((rf_read_spec_page_reg(PAGE0_SEL, 0x46) & 0x04) >> 2);

    freq = rf_read_freq();

    if ((freq >= freq_405000000) && (freq <= freq_565000000))
    {
        rf_efuse_on();
        pa_bias = rf_efuse_read_encry_byte(0x3b, 0x5aa5, 0x0d + 19);
        rf_efuse_off();
        if (pa_bias == 0)
        {
            pa_bias = 8;
        }
        pa_duty = rf_read_spec_page_reg(PAGE0_SEL, 0x45);
        for (i = 0; i < RF_MAX_RAMP + 1; i++)
        {
            if (open_ramp == power_ramp_cfg[i].ramp)
            {
                if (((pa_trim << 4) | pa_ldo) == power_ramp_cfg[i].pa_ldo)
                {
                    if ((pa_duty_en == true) && ((pa_duty + (power_ramp_cfg[i].pa_duty & 0x0f)) == ((power_ramp_cfg[i].pa_duty & 0xf0) + pa_bias)))
                    {
                        return i + 1;
                    }
                    else if ((pa_duty_en == false) && ((pa_duty | 0x70) == ((power_ramp_cfg[i].pa_duty & 0xf0) + pa_bias)))
                    {
                        return i + 1;
                    }
                }
            }
        }
        return 0;
    }
    else if ((freq >= freq_810000000) && (freq <= freq_890000000))
    {
        pwr_table = 2;
    }
    else if ((freq >= freq_890000000) && (freq <= freq_1080000000))
    {
        pwr_table = 3;
    }
    else
    {
        return FAIL;
    }

    for (i = 0; i < RF_MAX_RAMP; i++)
    {
        if (open_ramp == power_ramp[i][pwr_table].ramp)
        {
            if ((pa_trim == power_ramp[i][pwr_table].pa_trim) && (pa_ldo == power_ramp[i][pwr_table].pa_ldo))
            {
                if ((pa_duty_en == true) && (pa_duty == power_ramp[i][pwr_table].pa_duty))
                {
                    return i + 1;
                }
                else if ((pa_duty_en == false) && (0xff == power_ramp[i][pwr_table].pa_duty))
                {
                    return i + 1;
                }
            }
        }
    }

    return 0;
}

/**
 * @brief set preamble
 * @param[in] preamble
 * @return result
 */
RF_Err_t rf_set_preamble(uint16_t preamble)
{
    uint8_t temp[2] = {preamble & 0xff, (preamble >> 8) & 0xff};
    RF_ASSERT(rf_write_spec_page_regs(PAGE3_SEL, 0x13, temp, 2));

    return OK;
}

/**
 * @brief get preamble
 * @param[in] <none>
 * @return preamble
 */
uint16_t rf_get_preamble(void)
{
    uint8_t temp[2];

    rf_read_spec_page_regs(PAGE3_SEL, 0x13, temp, 2);

    return ((uint16_t)temp[1] << 8) | temp[0];
}

/**
 * @brief set RF GPIO as input
 * @param[in] <gpio_pin>  pin number of GPIO to be enable
 * @return result
 */
RF_Err_t rf_set_gpio_input(uint8_t gpio_pin)
{
    if(gpio_pin < 8)
    {
        RF_ASSERT(rf_set_spec_page_reg_bits(PAGE0_SEL, 0x63, (1 << gpio_pin)));
    }
    else
    {
        RF_ASSERT(rf_set_spec_page_reg_bits(PAGE0_SEL, 0x64, (1 << (gpio_pin - 8))));
    }

    return OK;
}

/**
 * @brief set RF GPIO as output
 * @param[in] <gpio_pin>  pin number of GPIO to be enable
 * @return result
 */
RF_Err_t rf_set_gpio_output(uint8_t gpio_pin)
{
    if(gpio_pin < 8)
    {
        RF_ASSERT(rf_set_spec_page_reg_bits(PAGE0_SEL, 0x65, (1 << gpio_pin)));
    }
    else
    {
        RF_ASSERT(rf_set_spec_page_reg_bits(PAGE0_SEL, 0x66, (1 << (gpio_pin - 8))));
    }

    return OK;
}

/**
 * @brief set GPIO output state, SET or RESET
 * @param[in] <gpio_pin>  pin number of GPIO to be opearted
 *			<state>   0  -  reset,
 *					  1  -  set
 * @return result
 */
RF_Err_t rf_set_gpio_state(uint8_t gpio_pin, uint8_t state)
{
    if(gpio_pin < 8)
    {
        RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x67, state, (1 << gpio_pin)));
    }
    else
    {
        RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x68, state, (1 << (gpio_pin - 8))));
    }

    return OK;
}

/**
 * @brief get GPIO input state
 * @param[in] <gpio_pin>  pin number of GPIO to be opearted
 *			<state>   0  -  low,
 *					  1  -  high
 * @return result
 */
bool rf_get_gpio_state(uint8_t gpio_pin)
{
    uint8_t tmpreg;

    if(gpio_pin < 6)
    {
        tmpreg = rf_read_spec_page_reg(PAGE0_SEL, 0x74);
    }
    else
    {
        tmpreg = rf_read_spec_page_reg(PAGE0_SEL, 0x75);
        gpio_pin -= 6;
    }

    return (bool)((tmpreg >> gpio_pin) & 0x01);
}

/**
 * @brief CAD function enable
 * @param[in] <none>
 * @return  result
 */
RF_Err_t rf_cad_on(uint8_t preamble_threshold,
                   uint8_t signal_power_thred,
                   uint8_t payload_threshold,
                   uint8_t payload_en)
{

		rf_set_gpio_output(11);
	
    /* GPIO11 -> CAD output */
    RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE0_SEL, 0x5E, BIT6));

    /* preamble CAD */
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE1_SEL, 0x25,
                                          preamble_threshold & 0x03,
                                          BIT0 | BIT1));

    /* payload CAD */
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE1_SEL, 0x35,
                                          signal_power_thred & 0x0F,
                                          BIT4 | BIT5 | BIT6 | BIT7));

    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE1_SEL, 0x35,
                                          payload_threshold & 0x03,
                                          BIT2 | BIT3));

    if (payload_en)
    {
        RF_ASSERT(rf_set_spec_page_reg_bits(PAGE1_SEL, 0x35, BIT1));
    }
    else
    {
        RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE1_SEL, 0x35, BIT1));
    }

    return OK;
}

/* @brief CAD function disable
* @param[in] <none>
* @return  result
*/
RF_Err_t rf_cad_off(void)
{
    RF_ASSERT(rf_set_spec_page_reg_bits(PAGE0_SEL, 0x5E, BIT6));
    RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x0f, 0x0a));

    return OK;
}

/**
 * @brief set rf syncword
 * @param[in] <sync> syncword
 * @return result
 */
RF_Err_t rf_set_syncword(uint8_t sync)
{
    RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, 0x0f, sync));
    
    return OK;
}

/**
 * @brief read rf syncword
 * @param[in] <none>
 * @return syncword
 */
uint8_t rf_get_syncword(void)
{
   return rf_read_spec_page_reg(PAGE3_SEL, 0x0f);
}

/**
 * @brief send one packet 发送一个数据包
 * @param[in] <buff> buffer contain data to send
 * @param[in] <len> the length of data to send
 * @return result
 */
RF_Err_t rf_send_packet(uint8_t *buff, int len)
{
    RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, REG_PAYLOAD_LEN, len));
    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_TX));

    rf_write_fifo(REG_FIFO_ACC_ADDR, buff, len);

    return OK;
}

/**
 * @brief receive a packet in non-block method, it will return 0 when no data got
 * @brief 收到一个数据包，在非阻塞方式下，若无数据则返回 0
 * @param[in] <buff> buffer provide for data to receive
 * @return length, it will return 0 when no data got
 */
uint8_t rf_recv_packet(uint8_t *buff)
{
    uint8_t len;

    len = rf_read_spec_page_reg(PAGE1_SEL, 0x7D);
    rf_read_fifo(REG_FIFO_ACC_ADDR, buff, len);

    return len;
}

/**
 * @brief set early interruption
 * @param[in] <earlyirq_val> PLHD IRQ to set
 *			  PLHD_IRQ_ON / PLHD_IRQ_OFF
 * @return result
 */
RF_Err_t rf_set_early_irq(bool NewState)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE1_SEL, 0x2B, NewState, BIT6));

    return OK;
}

/**
 * @brief read plhd irq status
 * @param[in] <none>
 * @return plhd irq status
 */
bool rf_get_early_irq(void)
{
    uint8_t tmpreg = rf_read_spec_page_reg(PAGE1_SEL, 0x2B);

    return (bool)(!!(tmpreg&BIT6));
}

/**
 * @brief set plhd
 * @param[in] <addr> PLHD start addr,Range:0..7f
 *			  <len> PLHD len
 *			  PLHD_LEN8 / PLHD_LEN16
 * @return result
 */
RF_Err_t rf_set_plhd(uint8_t addr, uint8_t len)
{
    uint8_t tmpreg = ((addr & 0x7f) | (len << 7));

    RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x2e, tmpreg));

    return OK;
}

/**
 * @brief get plhd len reg value
 * @param[in] <none>
 * @return <len> PLHD_LEN8 / PLHD_LEN16
 */
uint8_t rf_get_plhd_len(void)
{
    uint8_t tmpreg = rf_read_spec_page_reg(PAGE1_SEL, 0x2e);

    return ((tmpreg & 0x80) >> 7);
}

/**
 * @brief set plhd mask
 * @param[in] <plhd_val> plhd mask to set
 *			  PLHD_ON / PLHD_OFF
 * @return result
 */
RF_Err_t rf_set_plhd_mask(uint8_t plhd_val)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x58, plhd_val, BIT4));

    return OK;
}

/**
 * @brief read plhd mask
 * @param[in] <none>
 * @return plhd mask
 */
uint8_t rf_get_plhd_mask(void)
{
    uint8_t tmpreg = rf_read_spec_page_reg(PAGE0_SEL, 0x58);

    return tmpreg;
}

/**
 * @brief receive a packet in non-block method, it will return 0 when no data got
 * @param[in] <buff> buffer provide for data to receive
 *			  <len> PLHD_LEN8 / PLHD_LEN16
 * @return result
 */
uint8_t rf_plhd_receive(uint8_t *buf, uint8_t len)
{
    if (len == PLHD_LEN8)
    {
		rf_read_spec_page_regs(PAGE2_SEL, 0x76, buf, 8);
        return 8;
    }
    else if (len == PLHD_LEN16)
    {
        rf_read_spec_page_regs(PAGE2_SEL, 0x76, buf, 10);
        rf_read_spec_page_regs(PAGE0_SEL, 0x76, &buf[10], 6);
        return 16;
    }

    return 0;
}

/**
 * @brief set rf plhd mode on , rf will use early interruption
 * @param[in] <addr> PLHD start addr,Range:0..7f
		      <len> PLHD len
			  PLHD_LEN8 / PLHD_LEN16
 * @return result
 */
void rf_set_plhd_rx_on(uint8_t addr, uint8_t len)
{
    rf_set_early_irq(PLHD_IRQ_ON);
    rf_set_plhd(addr, len);
    rf_set_plhd_mask(PLHD_ON);
}

/**
 * @brief set rf plhd mode off
 * @param[in] <none>
 * @return result
 */
void rf_set_plhd_rx_off(void)
{
    rf_set_early_irq(PLHD_IRQ_OFF);
    rf_set_plhd_mask(PLHD_OFF);
}

/**
 * @brief set dcdc mode, The default configuration is DCDC_OFF, rf should set DCDC_OFF before enter sleep/deepsleep
 * @param[in] <dcdc_val> dcdc switch
 *			  DCDC_ON / DCDC_OFF
 * @return result
 */
RF_Err_t rf_set_dcdc_mode(uint8_t dcdc_val)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x24, dcdc_val, BIT3));

    return OK;
}

/**
 * @brief set LDR mode
 * @param[in] <mode> LDR switch
 *			  LDR_ON / LDR_OFF
 * @return result
 */
RF_Err_t rf_set_ldr(uint32_t mode)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x12, mode, BIT3));

    return OK;
}

/**
 * @brief get LDR mode
 * @param[in] <none>
 * @return result LDR_ON / LDR_OFF
 */
bool rf_get_ldr(void)
{
    uint8_t tmpreg = rf_read_spec_page_reg(PAGE3_SEL, 0x12);

    return (bool)(!!(tmpreg&BIT3));
}

int calculate_chirp_count(int sf_range[], int size, int chirp_counts[])
{
    int i, j;
    
    for (i = 0; i < size; i++) {
        int sf = sf_range[i];
        int fft_length = 1<<sf;
        int chirp_points = fft_length;
        int rx_points = 0;
        
        for (j = 0; j < size; j++) {
            int rx_sf = sf_range[j];
            int rx_chirp_points = (1<<rx_sf)* 2;
            rx_points += rx_chirp_points;
        }
        
        int preamble_length = rx_points;
        int quotient = preamble_length / chirp_points;
        int remainder = preamble_length % chirp_points;
        int chirp_count;
        
        if (remainder > 0) {
            chirp_count = (quotient + 1) + 2;  // Add 2 for safety margin
        } else {
            chirp_count = quotient + 2;
        }
        
        if (chirp_count < 8) {
            chirp_count = 8;
        }
        
        chirp_counts[i] = chirp_count;
    }
    
    return size;
}

/**
 * @brief set preamble by Spreading Factor,It is useful in all_sf_search mode
 * @param[in] <sf> Spreading Factor
 * @return result
 */
RF_Err_t rf_set_auto_sf_tx_preamble(int sf, int sf_range[], int size, int chirp_counts[])
{
	for (int i = 0; i < size; i++) {
		if( sf == sf_range[i])
		{
			RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, 0x13, (chirp_counts[i]& 0xff)));
			RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, 0x14, ((chirp_counts[i] >> 8) & 0xff)));
			return OK;
		}
	}
	
    return FAIL;
}

/**
 * @brief open all sf auto-search mode
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_set_auto_sf_rx_on(int sf_range[], int size)
{
    RF_ASSERT(rf_set_spec_page_reg_bits(PAGE3_SEL, 0x12, BIT0));
    RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x25, 0x04));
	
	uint8_t sf_mask = 0;
	for (int i = 0; i < size; i++) {

		sf_mask |= (1 << (sf_range[i] - 5));
		
	}

    RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x2d, sf_mask));

    return OK;
}

/**
 * @brief close all sf auto-search mode
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_set_auto_sf_rx_off(void)
{
    RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE3_SEL, 0x12, BIT0));
    RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, 0x14, 0));
    RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, 0x13, 8));

    return OK;
}

/**
 * @brief set carrier_wave mode on,Set BW and SF before calling this function
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_set_carrier_wave_on(void)
{
    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB3));

    RF_ASSERT(rf_set_tx_mode(RF_TX_CONTINOUS));
    RF_ASSERT(rf_set_tx_power(RF_MAX_RAMP));

    RF_ASSERT(rf_set_spec_page_reg_bits(PAGE1_SEL, 0x1E, BIT0));

    return OK;
}

/**
 * @brief set carrier_wave mode frequence and send carrier_wave
 * @param[in] <freq> RF frequency(in Hz) to set
 * @return result
 */
RF_Err_t rf_set_carrier_wave_freq(uint32_t freq)
{
    uint8_t buf[1];

    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB3));
    RF_ASSERT(rf_set_tx_mode(RF_TX_CONTINOUS));
    RF_ASSERT(rf_set_freq(freq));
    RF_ASSERT(rf_set_ldo_pa_on());

    rf_port.set_tx();

    RF_ASSERT(rf_send_packet(buf, 1));

    return OK;
}

/**
 * @brief set carrier_wave mode off
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_set_carrier_wave_off(void)
{
    RF_ASSERT(rf_write_reg(REG_OP_MODE, RF_MODE_STB3));
    RF_ASSERT(rf_set_ldo_pa_off());
    RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE1_SEL, 0x1E, BIT0));

    return OK;
}

/**
 * @brief set mapm mode enable
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_mapm_en(void)
{
    RF_ASSERT(rf_set_spec_page_reg_bits(PAGE1_SEL, 0x38, BIT0));

    return OK;
}

/**
 * @brief set mapm mode disable
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_mapm_dis(void)
{
    RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE1_SEL, 0x38, BIT0));

    return OK;
}

/**
 * @brief set mapm mask
 * @param[in] <mapm_val> mapm mask to set
 *			  MAPM_ON / MAPM_OFF
 * @return result
 */
RF_Err_t rf_set_mapm_mask(uint8_t mapm_val)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE0_SEL, 0x58, mapm_val, !BIT6));

    return OK;
}

/**
 * @brief get the number of fields
 * @param[in] <none>

 * @return <fn>
 */
uint8_t rf_get_mapm_field_num(void)
{
    uint8_t reg_fn, fn_h, fn_l, fn;

    reg_fn = rf_read_spec_page_reg(PAGE1_SEL, 0x3d);
    fn_h = ((reg_fn >> 4) - 1) * 15;
    fn_l = (reg_fn & 0x0f) - 1;
    fn = fn_h + fn_l;

    return fn;
}

/**
 * @brief set the number of fields(range in 0x01~0xe0)
 * @param[in] <fn> the number of fields you want to set

 * @return result
 */
RF_Err_t rf_set_mapm_field_num(uint8_t fn)
{
    uint8_t reg_fn, fn_h, fn_l;

    fn_h = fn / 15 + 1;
    fn_l = fn % 15 + 1;
    reg_fn = (fn_h << 4) + fn_l;
    RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x3d, reg_fn));

    return OK;
}

/**
 * @brief set the unit code word of the field counter represents several fields
 * @param[in] <fnm> the represents number you want to set
              0--1
              1--2
              2--4
              3--8
 * @return result
 */
RF_Err_t rf_set_mapm_field_num_mux(uint8_t fnm)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE1_SEL, 0x37, fnm, BIT7|BIT6));

    return OK;
}

/**
 * @brief set the last group function selection
 * @param[in] <group_fun_sel> The last group in the Field, its ADDR position function selection
 *             0:ordinary address      1:Field counter
 * @return result
 */
RF_Err_t rf_set_mapm_group_fun_sel(uint8_t gfs)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE1_SEL, 0x38, gfs, BIT1));

    return OK;
}

/**
 * @brief set the number of groups in Field
 * @param[in] <gn> the number of groups

 * @return result
 */
RF_Err_t rf_set_mapm_group_num(uint8_t gn)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE1_SEL, 0x38, gn, BIT3|BIT2));

    return OK;
}

/**
 * @brief set the number of Preambles in first groups
 * @param[in] <pgl> The numbers want set to Preambles in first groups(at least 10)

 * @return result
 */
RF_Err_t rf_set_mapm_firgroup_preamble_num(uint8_t pgl)
{
    RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x3b, pgl));

    return OK;
}

/**
 * @brief set the number of preambles for groups other than the first group
 * @param[in] <pgn>  the number of Preambles in other groups
 * @return result
 */
RF_Err_t rf_set_mapm_group_preamble_num(uint8_t pgn)
{
    RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x3c, pgn));

    return OK;
}

/**
 * @brief set group address1 of mapm mode
 * @param[in] <addr> The value of group address1 you want to set

 * @return result
 */
RF_Err_t rf_set_mapm_neces_preamble_num(uint16_t pn)
{
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE1_SEL, 0x39, (uint8_t)(pn >> 8), 0x0F));
    RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x3A, (uint8_t)(pn)));

    return OK;
}

/**
 * @brief set group address4 of mapm mode
 * @param[in] <addr> The value of group address4 you want to set

 * @return result
 */
RF_Err_t rf_set_mapm_addr(uint8_t addr_no, uint8_t addr)
{
    RF_ASSERT(rf_write_spec_page_reg(PAGE1_SEL, 0x3e + addr_no, addr));

    return OK;
}

/**
 * @brief calculate mapm preamble can sleep time
 * @param[in] <none>
 * @return sleeptime(ms)
 */
uint32_t rf_calculate_mapm_preambletime(stc_mapm_cfg_t *mapm_cfg, uint32_t one_chirp_time)
{
    uint8_t fnm, gn, pgn, pg1, fn, pn;
    uint16_t one_field_chirp, chirp_num;
    uint32_t preamble_time;

    pn = mapm_cfg->pn;
    pgn = mapm_cfg->pgn;
    pg1 = mapm_cfg->pg1;
    gn = mapm_cfg->gn;
    fnm = mapm_cfg->fnm;
    fn = mapm_cfg->fn;
    one_field_chirp = pg1 + 2 + (pgn + 2) * gn;
    chirp_num = (1 << fnm) * fn * one_field_chirp + pn - one_field_chirp;
    preamble_time = one_chirp_time * chirp_num;

    return preamble_time / 1000;
}

/**
 * @brief set rf mapm mode on , rf will use mapm interruption
 * @param[in] <none>
 * @return result
 */
void rf_set_mapm_on(void)
{
    rf_mapm_en();
    rf_set_mapm_mask(MAPM_ON);
}

/**
 * @brief set mapm mode off
 * @param[in] <none>
 * @return result
 */
void rf_set_mapm_off(void)
{
    rf_mapm_dis();
    rf_set_mapm_mask(MAPM_OFF);
}

/**
 * @brief configure relevant parameters used in mapm mode
 * @param[in] <p_mapm_cfg>
              <fn>set the number of fields(range in 0x01~0xe0)
              <field_num_mux> The unit code word of the Field counter represents several Fields
              <group_fun_sel> The last group in the Field, its ADDR position function selection
              0:ordinary address      1:Field counter
              <gn> register for configuring the number of groups in a Field
              0 1group\1 2group\2 3group\3 4group
              <pgl>set the number of Preambles in first groups>
              <pgn> the number of Preambles in other groups
              <pn> the number of chirps before syncword after all fields have been sent
 * @return result
 */
void rf_set_mapm_cfg(stc_mapm_cfg_t *p_mapm_cfg)
{
    rf_set_mapm_field_num(p_mapm_cfg->fn);
    rf_set_mapm_field_num_mux(p_mapm_cfg->fnm);
    rf_set_mapm_group_fun_sel(p_mapm_cfg->gfs);
    rf_set_mapm_group_num(p_mapm_cfg->gn);
    rf_set_mapm_firgroup_preamble_num(p_mapm_cfg->pg1);
    rf_set_mapm_group_preamble_num(p_mapm_cfg->pgn);
    rf_set_mapm_neces_preamble_num(p_mapm_cfg->pn);
}

/**
 * @brief efuse function enable
 * @param[in] <none>
 * @return  result
 */
RF_Err_t rf_efuse_on(void)
{
    RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE2_SEL, 0x3E, BIT3));

    return OK;
}

/**
 * @brief efuse function disable
 * @param[in] <none>
 * @return  result
 */
RF_Err_t rf_efuse_off(void)
{
    RF_ASSERT(rf_set_spec_page_reg_bits(PAGE2_SEL, 0x3E, BIT3));

    return OK;
}

/**
 * @brief read efuse area data in unencrypted mode
 * @param[in] <reg_addr> Efuse Register address, customer uses a fixed setting of 0x3c
              <efuse_addr> aaddress want to read data in efuse, customer's usage range is 0x2d~0x7f
 * @return data
 */
uint8_t rf_efuse_read_byte(uint8_t reg_addr, uint8_t efuse_addr)
{
    uint8_t value = 0;
    uint16_t timeout = 100;

    efuse_addr <<= 1;
    rf_switch_page(PAGE2_SEL);
    rf_write_fifo(reg_addr, &efuse_addr, 1);
    do
    {
        if (rf_read_spec_page_reg(PAGE0_SEL, 0x6c) & 0x80)
        {
            break;
        }
    } while (timeout--);

    rf_switch_page(PAGE2_SEL);
    rf_read_fifo(reg_addr, &value, 1);

    return value;
}

/**
 * @brief write efuse area data in unencrypted mode
 * @param[in] <reg_addr> Efuse Register address, customer uses a fixed setting of 0x3c
              <efuse_addr> address want to write data in efuse, customer's usage range is 0x2d~0x7f
              <value> data want to write in efuse
 * @return <none>
 */
void rf_efuse_write_byte(uint8_t reg_addr, uint8_t efuse_addr, uint8_t value)
{
    uint8_t data_buf[2];
    uint16_t timeout = 100;

    data_buf[0] = (efuse_addr << 1) | 0x01;
    data_buf[1] = value;

    rf_switch_page(PAGE2_SEL);
    rf_write_fifo(reg_addr, data_buf, 2);
    do
    {
        if (rf_read_spec_page_reg(PAGE0_SEL, 0x6c) & 0x80)
        {
            break;
        }
    } while (timeout--);
}

/**
 * @brief read efuse data for initialize
 * @return data
 */
uint8_t rf_efuse_read_encry_byte(uint8_t reg_addr, uint16_t pattern, uint8_t efuse_addr)
{
    uint8_t data_buf[3];
    uint8_t value = 0;
    uint16_t timeout = 100;

    data_buf[0] = pattern >> 8;
    data_buf[1] = pattern & 0xff;
    data_buf[2] = efuse_addr << 1;

    rf_switch_page(PAGE2_SEL);
    rf_write_fifo(reg_addr, data_buf, sizeof(data_buf));
    do
    {
        if (rf_read_spec_page_reg(PAGE0_SEL, 0x6C) & BIT7)
        {
            break;
        }
    } while (timeout--);
    rf_switch_page(PAGE2_SEL);
    rf_read_fifo(reg_addr, &value, 1);

    return value;
}

/**
 * @brief enable DCDC calibration
 * @param[in] <calibr_type> calibrated point
              1--ref calibration
              2--zero calibration
              3--imax calibration
 * @return result
 */
RF_Err_t rf_set_dcdc_calibr_on(uint8_t calibr_type)
{
    if ((calibr_type < CALIBR_REF_CMP) || (calibr_type > CALIBR_IMAX_CMP))
    {
        return FAIL;
    }

    uint8_t loop_time = 5;
    uint8_t dcdc_cal = 0;
    uint8_t rd_data, wr_data;
    uint8_t offset_reg_addr;

    if (calibr_type == CALIBR_ZERO_CMP)
    {
        offset_reg_addr = 0x1E;
    }
    else if (calibr_type == CALIBR_REF_CMP)
    {
        offset_reg_addr = 0x1D;
    }
    else if (calibr_type == CALIBR_IMAX_CMP)
    {
        offset_reg_addr = 0x1C;
    }

    /* calibration on */
    RF_ASSERT(rf_write_spec_page_reg_bits(PAGE3_SEL, 0x20, calibr_type, BIT5|BIT6));

    for (; loop_time > 0; loop_time--)
    {
        dcdc_cal |= (0x01 << (loop_time - 1));
        wr_data = 0x80 | dcdc_cal;
        RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, offset_reg_addr, wr_data));

        rd_data = rf_read_spec_page_reg(PAGE3_SEL, 0x27);
        if (rd_data & 0x01)
        {
            dcdc_cal &= ~(0x01 << (loop_time - 1));
        }
        else
        {
            dcdc_cal |= (0x01 << (loop_time - 1));
        }
        wr_data = 0x80 | dcdc_cal;
        RF_ASSERT(rf_write_spec_page_reg(PAGE3_SEL, offset_reg_addr, wr_data));
    }

    return OK;
}

/**
 * @brief disable DCDC calibration
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_set_dcdc_calibr_off(void)
{
    RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE3_SEL, 0x20, BIT5|BIT6));

    return OK;
}

/**
 * @brief enable LDO PA
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_set_ldo_pa_on(void)
{    
    RF_ASSERT(rf_set_spec_page_reg_bits(PAGE0_SEL, 0x4F, BIT3));

    return OK;
}

/**
 * @brief disable LDO PA
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_set_ldo_pa_off(void)
{
    RF_ASSERT(rf_reset_spec_page_reg_bits(PAGE0_SEL, 0x4F, BIT3));

    return OK;
}

/**
 * @brief rf enter rx continous mode to receive packet
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_enter_continous_rx(void)
{
    RF_ASSERT(rf_set_mode(RF_MODE_STB3));

    rf_port.set_rx();

    RF_ASSERT(rf_set_rx_mode(RF_RX_CONTINOUS));
    
    //变成接收模式
    RF_ASSERT(rf_set_mode(RF_MODE_RX));

    return OK;
}

/**
 * @brief rf enter rx single timeout mode to receive packet
 * @param[in] <timeout> rx single timeout time(in ms)
 * @return result
 */
RF_Err_t rf_enter_single_timeout_rx(uint32_t timeout)
{
    RF_ASSERT(rf_set_mode(RF_MODE_STB3));

    rf_port.set_rx();

    RF_ASSERT(rf_set_rx_mode(RF_RX_SINGLE_TIMEOUT));
    RF_ASSERT(rf_set_rx_single_timeout(timeout));
    RF_ASSERT(rf_set_mode(RF_MODE_RX));

    return OK;
}

/**
 * @brief rf enter rx single mode to receive packet
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_enter_single_rx(void)
{
    RF_ASSERT(rf_set_mode(RF_MODE_STB3));

    rf_port.set_rx();

    RF_ASSERT(rf_set_rx_mode(RF_RX_SINGLE));
    RF_ASSERT(rf_set_mode(RF_MODE_RX));

    return OK;
}

/**
 * @brief rf enter single tx mode and send packet
 * @param[in] <buf> buffer contain data to send
 * @param[in] <size> the length of data to send
 * @param[in] <tx_time> the packet tx time(us)
 * @return result
 */
RF_Err_t rf_single_tx_data(uint8_t *buf, uint8_t size, uint32_t *tx_time)
{
    RF_ASSERT(rf_set_mode(RF_MODE_STB3));
    RF_ASSERT(rf_set_ldo_pa_on());

    rf_port.set_tx();

    RF_ASSERT(rf_set_tx_mode(RF_TX_SINGLE));

    *tx_time = rf_get_tx_time(size);

    RF_ASSERT(rf_send_packet(buf, size));

    return OK;
}

/**
 * @brief rf enter continous tx mode to ready send packet
 * @param[in] <none>
 * @return result
 */
RF_Err_t rf_enter_continous_tx(void)
{
    RF_ASSERT(rf_set_mode(RF_MODE_STB3));//操作状态
    RF_ASSERT(rf_set_tx_mode(RF_TX_CONTINOUS));//配置发送设置

    return OK;
}

/**
 * @brief rf continous mode send packet
 * @param[in] <buf> buffer contain data to send
 * @param[in] <size> the length of data to send
 * @return result
 */
RF_Err_t rf_continous_tx_send_data(uint8_t *buf, uint8_t size)
{
//    RF_ASSERT(rf_set_ldo_pa_on());

    rf_port.set_tx();//发送端口

    RF_ASSERT(rf_send_packet(buf, size));

    return OK;
}

/**
 * @brief RF IRQ server routine, it should be call at ISR of IRQ pin
 * @param[in] <none>
 * @return result
 */
void rf_irq_process(void)//中断程序
{
    if(CHECK_IRQ())
    {
        uint8_t irq = rf_get_irq();
        if(irq & REG_IRQ_RX_PLHD_DONE)
        {
            RxDoneParams.PlhdSize = rf_get_plhd_len();
            RxDoneParams.PlhdSize = rf_plhd_receive(RxDoneParams.PlhdPayload, RxDoneParams.PlhdSize);
			irq &= ~REG_IRQ_RX_PLHD_DONE;
			rf_clr_irq(REG_IRQ_RX_PLHD_DONE);
			rf_set_recv_flag(RADIO_FLAG_PLHDRXDONE);
        }
        if(irq & REG_IRQ_MAPM_DONE)
        {
            uint8_t addr_val = rf_read_spec_page_reg(PAGE0_SEL, 0x6e);
            RxDoneParams.mpam_recv_buf[RxDoneParams.mpam_recv_index++] = addr_val;
			irq &= ~REG_IRQ_MAPM_DONE;
			rf_clr_irq(REG_IRQ_MAPM_DONE);
            rf_set_recv_flag(RADIO_FLAG_MAPM);
        }
        if(irq & REG_IRQ_RX_DONE)
        {
            RxDoneParams.Snr = rf_get_snr();
            RxDoneParams.Rssi = rf_get_rssi();
            RxDoneParams.Size = rf_recv_packet(RxDoneParams.Payload);
			irq &= ~REG_IRQ_RX_DONE;
			rf_clr_irq(REG_IRQ_RX_DONE);
			rf_set_recv_flag(RADIO_FLAG_RXDONE);
        }
        if(irq & REG_IRQ_CRC_ERR)
        {
            rf_read_fifo(REG_FIFO_ACC_ADDR, RxDoneParams.TestModePayload, 10);
			irq &= ~REG_IRQ_CRC_ERR;
			rf_clr_irq(REG_IRQ_CRC_ERR);
            rf_set_recv_flag(RADIO_FLAG_RXERR);
        }
        if(irq & REG_IRQ_RX_TIMEOUT)
        {
            rf_refresh();
			irq &= ~REG_IRQ_RX_TIMEOUT;
			rf_clr_irq(REG_IRQ_RX_TIMEOUT);
            rf_set_recv_flag(RADIO_FLAG_RXTIMEOUT);
        }
        if(irq & REG_IRQ_TX_DONE)
        {
            rf_set_ldo_pa_off();
			irq &= ~REG_IRQ_TX_DONE;
			rf_clr_irq(REG_IRQ_TX_DONE);
            rf_set_transmit_flag(RADIO_FLAG_TXDONE);
        }
    }
}

/**
 * @brief get one chirp time
 * @param[in] <bw>,<sf>
 * @return <time> us
 */
uint32_t rf_get_chirp_time(uint8_t bw, uint8_t sf)
{
    const uint32_t bw_table[4] = {62500, 125000, 250000, 500000};

    if(bw < BW_62_5K || bw > BW_500K)
    {
        return 0;
    }

    return (1000000 / bw_table[bw - BW_62_5K]) * (1 << sf);
}

/**
 * @brief check cad rx inactive
 * @param[in] <one_chirp_time>
 * @return <result> LEVEL_ACTIVE/LEVEL_INACTIVE
 */

bool check_cad_rx_inactive(uint32_t one_chirp_time)
{
	
//    rf_delay_us(one_chirp_time * 7);
//    rf_delay_us(360); // state machine start up time after enter rx state

    if (CHECK_CAD() != 1)
    {
//			rf_set_mode(RF_MODE_STB3);
      return LEVEL_INACTIVE;
    }

    return LEVEL_ACTIVE;
}




RF_Err_t rf_set_default_para(void)
{
		rf_param_t *rf_config_obj;
		rf_config_obj = (rf_param_t *)payload_GetObject();
	
		if(UART_DEBUG ==1){
		UART_DBG_Printf_DMA("freq:%d\r\n",rf_config_obj->freq);
		}

    RF_ASSERT(rf_set_freq(rf_config_obj->freq));//断言函数，rf_set_freq(DEFAULT_FREQ)
    
    RF_ASSERT(rf_set_code_rate(rf_config_obj->cr));//code rate 码率
    
    RF_ASSERT(rf_set_bw(rf_config_obj->bw));//带宽
    
    RF_ASSERT(rf_set_sf(rf_config_obj->sf));//扩频因子
    
    RF_ASSERT(rf_set_crc(CRC_ON));//循环冗余校验
    
    RF_ASSERT(rf_set_tx_power(rf_config_obj->pwr));//功率
    
    RF_ASSERT(rf_set_dcdc_mode(DCDC_OFF));//关闭DCDC供电寄存器
    
    return OK;
}




