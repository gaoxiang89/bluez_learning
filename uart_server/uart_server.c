
#include "uart_server.h"
#include "log.h"
#include "adapter.h"
#include <gio/gio.h>
#include <stdlib.h>
#include <glib.h>


#define BLUEZ_BUS_NAME "org.bluez"

static pthread_t pthread_hand;


static void *uart_server_process(void *arg)
{
	GMainLoop *loop;
	
	GError *error = NULL;
	loop = g_main_loop_new (NULL, FALSE);
	
	GDBusConnection *conn = g_bus_get_sync(
											G_BUS_TYPE_SYSTEM, 
											NULL, 
											&error);

	if(error) {
		g_error_free (error);
		return 0;
	}

	const char *bus_name = g_dbus_connection_get_unique_name(conn);
	u_tm_log("bus_name = %s\n", bus_name);

	adapter_power_on(conn);
	adapter_discoverable_enable(conn);

	u_tm_log("adapter_power_state = %d\n", adapter_power_state(conn));
	u_tm_log("adapter_discoverable_state = %d\n", adapter_discoverable_state(conn));
	
	/*
	 * 开始广播
	 */
	advertising_start(conn);

	/*
	 * 注册gatt server
	 */
	gatt_uart_server_start(conn);

	g_main_loop_run(loop);

	return 0;

}


void uart_server_init(uart_receive_t cb)
{
	static int is_init = 0;

	if(!is_init) {
		is_init = 1;
		/*
		 * 注册串口接收回调函数
	 	 */
		gatt_uart_register_receive_cb(cb);

		/*
		 * 启动ble uart 线程
		 */
		pthread_create(&pthread_hand, NULL, uart_server_process, NULL);
	}
}


void uart_server_send(uint8_t *buf, int len)
{
	gatt_uart_send(buf, len);
}



