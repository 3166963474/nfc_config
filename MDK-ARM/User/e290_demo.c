#include "e290_demo.h"

/* 指示传输是否完成的标志 */
extern struct RxDoneMsg RxDoneParams;


/* 发送函数 */
void e290_demo_transmit( uint8_t *buffer , uint8_t length )
{
    /* 进入连续发送 */
//	UART_DBG_Printf_DMA("Start Tx\r\n");
  rf_enter_continous_tx();
	rf_continous_tx_send_data( buffer , length );
}

/* 模组轮询任务 (状态机) */
void e290_demo_task(void)
{

}

