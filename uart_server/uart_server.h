
#ifdef __cplusplus
extern "C" {
#endif

#ifndef __UART_SERVER_H__
#define __UART_SERVER_H__

#include "gatt.h"
#include "advertising.h"

void uart_server_init(uart_receive_t cb);
void uart_server_send(uint8_t *buf, int len);


#endif
#ifdef __cplusplus
}
#endif


