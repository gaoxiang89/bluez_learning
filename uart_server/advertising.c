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
  * Date		Author		 Notes
  * 2021-04-09	huohongpeng  初次创建
  * 						 bluez默认的广播频率是1.28秒,所以搜索到设备比较慢
  *                          目前我在bluez 5.55里面发现有修改默认广播间隔的属性，但是没有实际测试过 
  */

#include"advertising.h"
#include <gio/gio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <stdint.h>

#include "log.h"

struct advertising_t {
	GDBusNodeInfo *node_info;
	GDBusInterfaceInfo *if_info;
	guint reg_id;
};

struct advertisement_data_t {
	char *LocalName;
	/* "broadcast" or "peripheral" */
	char *Type;
	/*
	 * 如果设置这个值为0,那么client将不能连接到设备
	 * adapter中也存在这个属性，但是广播中的Discoverable会覆盖adapter中的Discoverable
	 * 当Type为broadcast，不能设置这个属性
	 */
	uint8_t Discoverable;
	uint16_t DiscoverableTimeout;
};

static struct advertisement_data_t advertisement_data = {
	.LocalName = "bluez_uart",
	.Type = "peripheral", 
	.Discoverable = 1,
	.DiscoverableTimeout = 0,
};


static struct advertising_t advertising_ctx;

#define ADVERT_OBJ_PATH "/org/uart/advertising"


static const gchar advertising_xml[] =
"<node>"
"  <interface name='org.bluez.LEAdvertisement1'>"
"    <method name='Release'/>"
"    <property name='LocalName' type='s' access='read'/>"
"    <property name='Type' type='s' access='read'/>"
"	 <property name='ServiceUUIDs' type='as' access='read'/>"
"    <property name='Discoverable' type='b' access='read'/>"
"    <property name='DiscoverableTimeout' type='q' access='read'/>"
"  </interface>"
"</node>";



/*
 * 1.通过xml数据创建node info
 * 2.通过node info获取interface info,interface info 用于注册DBUS对象
 */
static int advertising_get_interface_info(const gchar *xml_data)
{
	GError *error = NULL;

	GDBusNodeInfo *node_info = g_dbus_node_info_new_for_xml(xml_data, &error);

	if(error) {
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}

	if(!node_info) {
		u_tm_log("node info NULL\n");
		return -1;
	}

	advertising_ctx.node_info = node_info;

	return 0;
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
	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, method_name);

	if(!strcmp(method_name, "Release")) {
		
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
	u_tm_log("[%s:%d] sender :%s\n", __FUNCTION__, __LINE__, sender);
	u_tm_log("[%s:%d] object_path :%s\n", __FUNCTION__, __LINE__, object_path);
	u_tm_log("[%s:%d] interface_name :%s\n", __FUNCTION__, __LINE__, interface_name);
	u_tm_log("[%s:%d] property_name :%s\n", __FUNCTION__, __LINE__, property_name);

	GVariant *v = NULL;
	if(!strcmp(property_name, "LocalName")) {
		v = g_variant_new("s", advertisement_data.LocalName);
	} else if(!strcmp(property_name, "Type")) {
		v = g_variant_new("s", "peripheral"); /* "broadcast" or "peripheral" */
	} else if(!strcmp(property_name, "ServiceUUIDs")) {
		  GVariantBuilder *builder = g_variant_builder_new(G_VARIANT_TYPE("as"));

		  g_variant_builder_add(builder, "s", "6e400001-b5a3-f393-e0a9-e50e24dcca9e");
		  v= g_variant_builder_end(builder);
		  g_variant_builder_unref(builder);
	} else if(!strcmp(property_name, "Discoverable")) {
		v = g_variant_new("b", advertisement_data.Discoverable);
	} else if(!strcmp(property_name, "DiscoverableTimeout")) {
		v = g_variant_new("q", advertisement_data.DiscoverableTimeout);
	}
	
	return v;
}
															 

static int advertising_object_register(GDBusConnection *conn)
{
	GError *error = NULL;
	GDBusInterfaceVTable interface_vtable;

	/*
	 * 这些回调函数的执行是依赖于g_main_loop的
	 */
	interface_vtable.method_call = on_method_call;
	interface_vtable.get_property = get_property;
	interface_vtable.set_property = NULL;

	guint reg_id = 
		g_dbus_connection_register_object(conn,
                                   		ADVERT_OBJ_PATH,
                                   		advertising_ctx.node_info->interfaces[0],/*org.bluez.LEAdvertisement1*/
                                   		&interface_vtable,
                                   		NULL,
                                   		NULL,
                                   		&error);
	if(error) {
		u_tm_log("<org.bluez.LEAdvertisement1> interface info register Error\n");
		u_tm_log("%s\n", error->message);
		g_error_free (error);
		return -1;
	}

	advertising_ctx.reg_id = reg_id;

	return 0;
}


static void async_ready_callback(GObject *source_object,
                        GAsyncResult *res,
                        gpointer user_data)
{
	u_tm_log("async_ready_callback\n");
	GDBusConnection *conn = (GDBusConnection *)user_data;
	GError *error = NULL;
	
	g_dbus_connection_call_finish (conn,
                               res,
                               &error);

   if(error) {
	   u_tm_log("RegisterAdvertisement Error\n");
	   u_tm_log("%s\n", error->message);
	   g_error_free (error);
   }
}


static int advertising_register_to_bluez_async(GDBusConnection *conn)
{
	GVariant *parameters;
	
	GVariant *vobject_path = g_variant_new("o", ADVERT_OBJ_PATH);

	GVariantBuilder *dict_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));
	g_variant_builder_add(dict_builder, "{sv}", "param", g_variant_new_string("value"));
	
	GVariant *dict_v = g_variant_builder_end(dict_builder);
	g_variant_builder_unref(dict_builder);
	
	GVariant *children[] = {vobject_path, dict_v};
	parameters = g_variant_new_tuple(children, 2);

	g_dbus_connection_call (conn,
							"org.bluez",
							"/org/bluez/hci1",
							"org.bluez.LEAdvertisingManager1",
							"RegisterAdvertisement",
							parameters,
							NULL,
							G_DBUS_CALL_FLAGS_NONE,
							-1,
							NULL,
							async_ready_callback,
							conn);
	
	return 0;						
}


int advertising_start(GDBusConnection *conn)
{
	advertising_get_interface_info(advertising_xml);
	advertising_object_register(conn);
	advertising_register_to_bluez_async(conn);
	
	return 0;
}


