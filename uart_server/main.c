#include <stdio.h>
#include "log.h"
#include "uart_server.h"
#include <stdint.h>


/*
 * 将接收到的数据通过串口回显
 */
static void uart_receive_func(uint8_t *buf, int len)
{
	u_tm_log_hex("uart rx: ", buf, len);
	uart_server_send(buf, len);
}



int main(void)
{
	uart_server_init(uart_receive_func);

	while(1) {
		sleep(1);
	}
	
}
