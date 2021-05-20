#ifdef __cplusplus
 extern "C" {
#endif

#ifndef __GATT_H__
#define __GATT_H__

#include <stdint.h>
#include <gio/gio.h>

typedef void (*uart_receive_t)(uint8_t *buf, int len);

int gatt_uart_server_start(GDBusConnection *conn);
void gatt_uart_register_receive_cb(uart_receive_t receive_cb);
void gatt_uart_send(uint8_t *buf, int len);


#endif
#ifdef __cplusplus
}
#endif



