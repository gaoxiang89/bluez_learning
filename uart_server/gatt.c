/*
 * Copyright (C) 2021, 2021  huohongpeng
 * Author: huohongpeng <1045338804@qq.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Change logs:
 * Date        Author       Notes
 * 2021-04-12  huohongpeng  初次创建
 *							1.蓝牙版本bluez5.54
 *							2.实现了uart的gatt service
 *							3.为了便于使用Nordic的APP(nRF Connect)调试,程序中的UUID使用的都是Nodic的uart的UUID
 */

#include <gio/gio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <stdint.h>

#include "gatt.h"
#include "log.h"

//#define __DEBUG__

#define UART_OBJECT_PATH "/org/uart/server"

static const gchar object_manager_xml[] =
"<node>"
"  <interface name='org.freedesktop.DBus.ObjectManager'>"
"    <method name='GetManagedObjects'>"
"      <arg name='objects' type='a{oa{sa{sv}}}' direction='out'/>"
"    </method>"
"    <signal name='InterfacesRemoved'>"
"      <arg name='interfaces' type='as'/>"
"    </signal>"
"  </interface>"
"</node>";


/*
 * 服务对象的xml
 */
static const gchar service_xml[] =
"<node>"
"  <interface name='org.bluez.GattService1'>"
"    <property name='UUID' type='s' access='read'/>"
"    <property name='Primary' type='b' access='read'/>"
"  </interface>"
"</node>";


/*
 * 特性对象的xml
 */
static const gchar char_xml[] = 
"<node>"
"  <interface name='org.bluez.GattCharacteristic1'>"
"    <property name='UUID' type='s' access='read'/>"
"    <property name='Service' type='o' access='read'/>"
"    <property name='Value' type='ay' access='read'/>"
"    <property name='Notifying' type='b' access='read'/>"
"    <property name='Flags' type='as' access='read'/>"
"    <method name='ReadValue'>"
"      <arg name='options' type='a{sv}' direction='in'/>"
"      <arg name='value' type='ay' direction='out'/>"
"    </method>"
"    <method name='WriteValue'>"
"      <arg name='value' type='ay' direction='in'/>"
"      <arg name='options' type='a{sv}' direction='in'/>"
"    </method>"
"    <method name='StartNotify'>"
"    </method>"
"    <method name='StopNotify'/>"
"  </interface>"
"</node>";


#define CHAR_FLAGS_SIZE 17

struct char_t {
	char *UUID;
	char *Service;
	uint8_t Value[512];
	int len;
	/* @Flags:
	 *	"broadcast"
	 *	"read"
	 *	"write-without-response"
	 *	"write"
	 *	"notify"
	 *	"indicate"
	 *	"authenticated-signed-writes"
	 *	"extended-properties"
	 *	"reliable-write"
	 *	"writable-auxiliaries"
	 *	"encrypt-read"
	 *	"encrypt-write"
	 *	"encrypt-authenticated-read"
	 *	"encrypt-authenticated-write"
	 *	"secure-read" (Server only)
	 *	"secure-write" (Server only)
	 *	"authorize"
	 */
	char *Flags[CHAR_FLAGS_SIZE];
	/*
	 * True, if notifications or indications on this
	 * characteristic are currently enabled.
	 */
	int Notifying;
	
};


struct service_t {
	char *UUID;
	/*
	 * Indicates whether or not this GATT service is a
	 * primary service. If false, the service is secondary.
	 */
	int Primary;
};


struct server_t {
	struct {
		struct service_t service;
		struct char_t rx_char;
		struct char_t tx_char;
	} gatt;

	GDBusConnection *conn;
	GDBusNodeInfo *object_manager_node_info;
	guint object_manager_reg_id;
	GDBusNodeInfo *service_node_info;
	guint service_reg_id;
	GDBusNodeInfo *char_node_info;
	guint tx_char_reg_id;
	guint rx_char_reg_id;
	uart_receive_t receive_cb_func;
};



/*
-> /org/uart/server
  |   - org.freedesktop.DBus.ObjectManager
  |
  -> /org/uart/server/service00
  | |   - org.freedesktop.DBus.Properties
  | |   - org.bluez.GattService1
  | |
  | -> /org/uart/server/service00/char0000
  | |     - org.freedesktop.DBus.Properties
  | |     - org.bluez.GattCharacteristic1
  | |
  | -> /org/uart/server/service00/char0001
  |   |   - org.freedesktop.DBus.Properties
  |   |   - org.bluez.GattCharacteristic1
  |   |
  |   -> /org/uart/server/service00/char0001/desc000 (cccd被bluez自动创建)
  |       - org.freedesktop.DBus.Properties
  |       - org.bluez.GattDescriptor1
  |
  -> /org/uart/server/serviceXX
    |   - org.freedesktop.DBus.Properties
    |   - org.bluez.GattService1
    |
    -> /org/uart/server/serviceXX/char0000
        - org.freedesktop.DBus.Properties
        - org.bluez.GattCharacteristic1
*/



static struct server_t server_ctx = {
	.gatt = {
		.service = {
			.UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e",
			.Primary = 1,
		},
		/*
		 * "/service00/char0000"
		 */
		.rx_char = {
			.UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e",
			.Service = UART_OBJECT_PATH"/service00",
			.Flags = {
				[0] = "write-without-response",
				//[1] = "read",
			},
		},
		/*
		 * "/service00/char0001"
		 */
		.tx_char = {
			.UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e",
			.Service = UART_OBJECT_PATH"/service00",
			.Flags = {
				[0] = "notify",
			},
		},
	},
};


void gatt_uart_register_receive_cb(uart_receive_t receive_cb)
{
	server_ctx.receive_cb_func = receive_cb;
}


/*
 * 最大发送512个字节
 */
void gatt_uart_send(uint8_t *buf, int len)
{
	if(!server_ctx.conn || !server_ctx.gatt.tx_char.Notifying || len > 512) {
		return ;
	}

	memcpy(server_ctx.gatt.tx_char.Value, buf, len);
	server_ctx.gatt.tx_char.len = len;

	/*
	 * 通知是通过PropertiesChanged信号实现的。
	 * 当bluez收到Value属性PropertiesChanged的信号,
	 * bluez就会向客户端发送notification  或者 indication。
	 *
	 * 参考:doc/gatt-api.txt
	 * The cached value of the characteristic. This property
	 * gets updated only after a successful read request and
	 * when a notification or indication is received, upon
	 * which a PropertiesChanged signal will be emitted.
	 */
	GVariant *parameters[3];
	
	/*
	 * interface_name
	 */
	parameters[0] = g_variant_new_string("org.bluez.GattCharacteristic1");

	/*
	 * changed_properties
	 */
	GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
	int i;
	for(i = 0; i < server_ctx.gatt.tx_char.len; i++) {
		g_variant_builder_add(builder, "y", server_ctx.gatt.tx_char.Value[i]);
	}
	
	GVariant *value = g_variant_builder_end(builder);
	g_variant_builder_unref(builder);

	GVariantBuilder *prop_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(prop_builder, "{sv}", "Value", value);
	parameters[1] = g_variant_builder_end(prop_builder);
	g_variant_builder_unref(prop_builder);

	/*
	 * invalidated_properties
	 */
	GVariantBuilder *inv_prop_builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
	parameters[2] = g_variant_builder_end(inv_prop_builder);
	g_variant_builder_unref(inv_prop_builder);

	GError *error = NULL;
	g_dbus_connection_emit_signal(server_ctx.conn,
                               "org.bluez",
                               UART_OBJECT_PATH"/service00/char0001",
                               "org.freedesktop.DBus.Properties",
                               "PropertiesChanged" ,
                               g_variant_new_tuple(parameters, 3), /* (sa{sv}as) */
                               &error);
	if(error) {
		u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
		g_error_free (error);
	}						   
}


static int gatt_create_node_info(void)
{
	GError *error = NULL;

	server_ctx.object_manager_node_info = g_dbus_node_info_new_for_xml(object_manager_xml, &error);

	if(error) {
		u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
		g_error_free (error);
		return -1;
	}

	server_ctx.service_node_info = g_dbus_node_info_new_for_xml(service_xml, &error);

	if(error) {
		u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
		g_error_free (error);
		goto ERROR_1;
	}

	server_ctx.char_node_info = g_dbus_node_info_new_for_xml(char_xml, &error);

	if(error) {
		u_tm_log("[%s:%d] error: %s\n", __FUNCTION__, __LINE__, error->message);
		g_error_free (error);
		goto ERROR_2;
	}
	
	u_tm_log("[%s:%d] %s\n", __FUNCTION__, __LINE__, "gatt_create node info ok");
	return 0;

ERROR_2:
	g_dbus_node_info_unref(server_ctx.service_node_info);

ERROR_1:
	g_dbus_node_info_unref(server_ctx.object_manager_node_info);
	return -1;
	
}



static GVariant *
get_property_variant(const gchar *object_path, const gchar *interface_name, const gchar *property_name)
{
	GVariant *v = NULL;

	if(!strcmp(object_path, UART_OBJECT_PATH"/service00")) {
		if(!strcmp(property_name, "UUID")) {
			v = g_variant_new("s", server_ctx.gatt.service.UUID);
		} else if(!strcmp(property_name, "Primary")) {
			v = g_variant_new("b", server_ctx.gatt.service.Primary);
		}
	} else if(!strcmp(object_path, UART_OBJECT_PATH"/service00/char0000")) {
		if(!strcmp(property_name, "UUID")) {
			v = g_variant_new("s", server_ctx.gatt.rx_char.UUID);
		} else if(!strcmp(property_name, "Flags")) {
			GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
			int i;
			for(i = 0; i < CHAR_FLAGS_SIZE; i++) {
				if(server_ctx.gatt.rx_char.Flags[i])
					g_variant_builder_add(builder, "s", server_ctx.gatt.rx_char.Flags[i]);
			}
			v= g_variant_builder_end(builder);
			g_variant_builder_unref(builder);
		} else if(!strcmp(property_name, "Value")) {
			GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
			int i;
			for(i = 0; i < server_ctx.gatt.rx_char.len; i++) {
				g_variant_builder_add(builder, "y", server_ctx.gatt.rx_char.Value[i]);
			}
			
			v= g_variant_builder_end(builder);
			g_variant_builder_unref(builder);
		} else if(!strcmp(property_name, "Service")) {
			v = g_variant_new("o", server_ctx.gatt.rx_char.Service);
		}  else if(!strcmp(property_name, "Notifying")) {
			v = g_variant_new("b", server_ctx.gatt.rx_char.Notifying);
		}
	} else if(!strcmp(object_path, UART_OBJECT_PATH"/service00/char0001")) {
		if(!strcmp(property_name, "UUID")) {
			v = g_variant_new("s", server_ctx.gatt.tx_char.UUID);
		} else if(!strcmp(property_name, "Flags")) {
			GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("as"));
			int i;
			for(i = 0; i < CHAR_FLAGS_SIZE; i++) {
				if(server_ctx.gatt.tx_char.Flags[i])
					g_variant_builder_add(builder, "s", server_ctx.gatt.tx_char.Flags[i]);
			}
			v= g_variant_builder_end(builder);
			g_variant_builder_unref(builder);
		} else if(!strcmp(property_name, "Value")) {
			GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("ay"));
			int i;
			for(i = 0; i < server_ctx.gatt.tx_char.len; i++) {
				g_variant_builder_add(builder, "y", server_ctx.gatt.tx_char.Value[i]);
			}
			
			v= g_variant_builder_end(builder);
			g_variant_builder_unref(builder);
		} else if(!strcmp(property_name, "Service")) {
			v = g_variant_new("o", server_ctx.gatt.tx_char.Service);
		} else if(!strcmp(property_name, "Notifying")) {
			v = g_variant_new("b", server_ctx.gatt.tx_char.Notifying);
		}
	}

	return v;

}

/*
 * type='a{oa{sa{sv}}}'
 */
static GVariant *gatt_create_managed_objects(void)
{
	GVariant *v_property, *v;
	GVariantBuilder *builder_if;
	GVariantBuilder *builder_obj;
	GVariantBuilder *builder_mobjs;
	int i;

	builder_mobjs = g_variant_builder_new(G_VARIANT_TYPE("a{oa{sa{sv}}}"));

	/*
	 * 构建/org/uart/server/service00的接口和属性
	 */
	builder_if = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	
	v_property = get_property_variant(UART_OBJECT_PATH"/service00", "org.bluez.GattService1", "UUID");
	g_variant_builder_add(builder_if, "{&sv}", "UUID", v_property);

	v_property = get_property_variant(UART_OBJECT_PATH"/service00", "org.bluez.GattService1", "Primary");
	g_variant_builder_add(builder_if, "{&sv}", "Primary", v_property);
	
	v = g_variant_builder_end(builder_if);
	g_variant_builder_unref(builder_if);

	builder_obj = g_variant_builder_new(G_VARIANT_TYPE("a{sa{sv}}"));
	
	g_variant_builder_add(builder_obj, "{&s@a{sv}}", "org.bluez.GattService1", v);
	
	v = g_variant_builder_end(builder_obj);
	g_variant_builder_unref(builder_obj);

	g_variant_builder_add(builder_mobjs, "{&o@a{sa{sv}}}", UART_OBJECT_PATH"/service00", v);

	/*
	 * 构建/org/uart/server/service00/char0000的接口和属性
	 */
	builder_if = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	const char *char0000_list[] = {"UUID", "Service", "Value", "Notifying", "Flags"};

	for(i = 0; i < sizeof(char0000_list)/sizeof(const char *); i++) {
		v_property = get_property_variant(UART_OBJECT_PATH"/service00/char0000", "org.bluez.GattCharacteristic1", char0000_list[i]);
		g_variant_builder_add(builder_if, "{&sv}", char0000_list[i], v_property);
	}
	
	v = g_variant_builder_end(builder_if);
	g_variant_builder_unref(builder_if);
	
	builder_obj = g_variant_builder_new(G_VARIANT_TYPE("a{sa{sv}}"));
	
	g_variant_builder_add(builder_obj, "{&s@a{sv}}", "org.bluez.GattCharacteristic1", v);
	
	v = g_variant_builder_end(builder_obj);
	g_variant_builder_unref(builder_obj);
	
	g_variant_builder_add(builder_mobjs, "{&o@a{sa{sv}}}", UART_OBJECT_PATH"/service00/char0000", v);

	/*
	 * 构建/org/uart/server/service00/char0001的接口和属性
	 */
	builder_if = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

	const char *char0001_list[] = {"UUID", "Service", "Value", "Notifying", "Flags"};

	for(i = 0; i < sizeof(char0001_list)/sizeof(const char *); i++) {
		v_property = get_property_variant(UART_OBJECT_PATH"/service00/char0001", "org.bluez.GattCharacteristic1", char0001_list[i]);
		g_variant_builder_add(builder_if, "{&sv}", char0001_list[i], v_property);
	}
	
	v = g_variant_builder_end(builder_if);
	g_variant_builder_unref(builder_if);
	
	builder_obj = g_variant_builder_new(G_VARIANT_TYPE("a{sa{sv}}"));
	
	g_variant_builder_add(builder_obj, "{&s@a{sv}}", "org.bluez.GattCharacteristic1", v);
	
	v = g_variant_builder_end(builder_obj);
	g_variant_builder_unref(builder_obj);
	
	g_variant_builder_add(builder_mobjs, "{&o@a{sa{sv}}}", UART_OBJECT_PATH"/service00/char0001", v);
	
	/*
	 * 构建GetManagedObjects返回值类型
	 */
	v = g_variant_builder_end(builder_mobjs);
	g_variant_builder_unref(builder_mobjs);

	GVariant *tuples[] = {v};
	
	return g_variant_new_tuple(tuples, 1);
}

static void uart_rx_callback(GVariant *params)
{
#ifdef __DEBUG__
	u_tm_log("params type: \"%s\"\n", g_variant_get_type_string(params));
#endif

	/*
	 * params type: "(aya{sv})"
	 */

	GVariant *value, *flags;
	g_variant_get(params, "(@ay@a{sv})", &value, &flags);
	u_tm_log("value type: \"%s\"\n", g_variant_get_type_string(value));
	u_tm_log("flags type: \"%s\"\n", g_variant_get_type_string(flags));
	
	GVariantIter *iter = g_variant_iter_new(value);

	/*
	 * 提取数据
	 */
	server_ctx.gatt.rx_char.len = 0;
	uint8_t *pdata = server_ctx.gatt.rx_char.Value;
	
	while(g_variant_iter_next(iter, "y", (pdata + server_ctx.gatt.rx_char.len)))
	{
		server_ctx.gatt.rx_char.len++;
	}
	g_variant_iter_free(iter);
	
#ifdef __DEBUG__
	u_tm_log("uart_rx_callback len: %d\n", server_ctx.gatt.rx_char.len);
	u_tm_log_hex("uart_rx_callback value: ", server_ctx.gatt.rx_char.Value, server_ctx.gatt.rx_char.len);
#endif

	/*
	 * 将数据提供给回调函数
	 */
	if(server_ctx.gatt.rx_char.len && server_ctx.receive_cb_func) {
		server_ctx.receive_cb_func(server_ctx.gatt.rx_char.Value, server_ctx.gatt.rx_char.len);
	}
}


static void 
on_method_call(GDBusConnection *con,
                       const gchar *sender,
                       const gchar *obj_path,
                       const gchar *iface_name,
                       const gchar *method_name,
                       GVariant *params,
                       GDBusMethodInvocation *invoc,
                       gpointer udata)
{
#ifdef __DEBUG__
	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, obj_path);
	u_tm_log("[%s:%d] iface_name :%s\n", __FUNCTION__, __LINE__, iface_name);
	u_tm_log("[%s:%d] method_name :%s\n", __FUNCTION__, __LINE__, method_name);
#endif

	if(!strcmp(obj_path, UART_OBJECT_PATH)) {
		if(!strcmp(method_name, "GetManagedObjects")) {
			g_dbus_method_invocation_return_value(invoc, gatt_create_managed_objects());
		}
	} else if(!strcmp(obj_path, UART_OBJECT_PATH"/service00/char0000")) {/* Rx */
		if(!strcmp(method_name, "WriteValue")) {
			uart_rx_callback(params);
		}
	} else if(!strcmp(obj_path, UART_OBJECT_PATH"/service00/char0001")) {/* Tx */
		if(!strcmp(method_name, "StartNotify")) {
			server_ctx.gatt.tx_char.Notifying = 1;
			u_tm_log("Start server_ctx.gatt.tx_char.Notifying = %d\n", server_ctx.gatt.tx_char.Notifying);
		} else if(!strcmp(method_name, "StopNotify")) {
			server_ctx.gatt.tx_char.Notifying = 0;
			u_tm_log("Stop server_ctx.gatt.tx_char.Notifying = %d\n", server_ctx.gatt.tx_char.Notifying);
		}
	} 
}

/*
 * 如果interface info中有可读的属性存在,那么必须提供一个非空的get_property,
 * 或者在org.freedesktop.DBus.Properties接口上的method_call方法中实现Get和GetAll两个函数.
 * 
 */
static GVariant *
get_property(GDBusConnection *connection,
					const gchar *sender,
					const gchar *object_path,
					const gchar *interface_name,
					const gchar *property_name,
					GError **error,
					gpointer user_data)
{
#ifdef __DEBUG__
	u_tm_log("[%s:%d] sender :%s\n", __FUNCTION__, __LINE__, sender);
	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, object_path);
	u_tm_log("[%s:%d] interface_name :%s\n", __FUNCTION__, __LINE__, interface_name);
	u_tm_log("[%s:%d] property_name :%s\n", __FUNCTION__, __LINE__, property_name);
#endif

	GVariant *v = get_property_variant(object_path, 
										interface_name, 
										property_name);

	return v;
}




static int gatt_object_register(GDBusConnection *conn)
{
	GError *error = NULL;
	GDBusInterfaceVTable interface_vtable;

	/*
	 * 这些回调函数的执行是依赖于g_main_loop的
	 */
	interface_vtable.method_call = on_method_call;
	interface_vtable.get_property = NULL; /*ObjectManager 不存在属性*/
	interface_vtable.set_property = NULL;

  
	server_ctx.object_manager_reg_id = 
		g_dbus_connection_register_object(conn,
                                   		UART_OBJECT_PATH,
                                   		server_ctx.object_manager_node_info->interfaces[0],/*org.freedesktop.DBus.ObjectManager*/
                                   		&interface_vtable,
                                   		NULL,
                                   		NULL,
                                   		&error);
	if(error) {
		u_tm_log("<org.freedesktop.DBus.ObjectManager> interface info register Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}


	interface_vtable.method_call = on_method_call;
	interface_vtable.get_property = get_property;
	interface_vtable.set_property = NULL;
	
	server_ctx.service_reg_id = 
		g_dbus_connection_register_object(conn,
                                   		UART_OBJECT_PATH"/service00",
                                   		server_ctx.service_node_info->interfaces[0],/*org.bluez.GattService1*/
                                   		&interface_vtable,
                                   		NULL,
                                   		NULL,
                                   		&error);
	if(error) {
		u_tm_log("<org.bluez.GattService1> interface info register Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}

	server_ctx.rx_char_reg_id = 
		g_dbus_connection_register_object(conn,
                                   		UART_OBJECT_PATH"/service00/char0000",
                                   		server_ctx.char_node_info->interfaces[0],/*org.bluez.GattCharacteristic1*/
                                   		&interface_vtable,
                                   		NULL,
                                   		NULL,
                                   		&error);
	if(error) {
		u_tm_log("<RX org.bluez.GattCharacteristic1> interface info register Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}

	server_ctx.tx_char_reg_id = 
		g_dbus_connection_register_object(conn,
                                   		UART_OBJECT_PATH"/service00/char0001",
                                   		server_ctx.char_node_info->interfaces[0],/*org.bluez.GattCharacteristic1*/
                                   		&interface_vtable,
                                   		NULL,
                                   		NULL,
                                   		&error);
	if(error) {
		u_tm_log("<TX org.bluez.GattCharacteristic1> interface info register Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}
	
	u_tm_log("[%s:%d] %s\n", __FUNCTION__, __LINE__, "gatt object register ok");
	
	return 0;
}

static void async_ready_callback(GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data)
{
	
	GDBusConnection *conn = (GDBusConnection *)user_data;
	GError *error = NULL;
	
	g_dbus_connection_call_finish (conn,
                               res,
                               &error);

   if(error) {
	   u_tm_log("Error: RegisterApplication %s\n", error->message);
	   g_error_free (error);
   		return;
   }

   u_tm_log("async_ready_callback: uart_register_application ok \n");
}



static void uart_register_application_async(GDBusConnection *conn)
{
	GVariant *parameters;
	

	GVariant *vobject_path = g_variant_new("o", UART_OBJECT_PATH);

	GVariantBuilder *dict_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	GVariant *dict_v = g_variant_builder_end(dict_builder);
	g_variant_builder_unref(dict_builder);
	
	GVariant *children[] = {vobject_path, dict_v};
	parameters = g_variant_new_tuple(children, 2);

	g_dbus_connection_call (conn,
							"org.bluez",
							"/org/bluez/hci1",
							"org.bluez.GattManager1",
							"RegisterApplication",
							parameters,
							NULL,
							G_DBUS_CALL_FLAGS_NONE,
							-1,
							NULL,
							async_ready_callback,
							conn);	
}


int gatt_uart_server_start(GDBusConnection *conn)
{
	gatt_create_node_info();
	gatt_object_register(conn);
	uart_register_application_async(conn);
	server_ctx.conn = conn;
	return 0;
}





