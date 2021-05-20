
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
 * 2020-04-14  huohongpeng  初次创建
 *							1.蓝牙版本bluez5.54
 *							2.基于bluez adapter-api.txt中的接口实现部分功能
 */	


#include <gio/gio.h>
#include <stdlib.h>
#include <glib.h>
#include <string.h>
#include <stdint.h>
 
#include "log.h"


static void adapter_properties_set(GDBusConnection *conn, char *interface, char *name, GVariant *value)
{
	GError *error = NULL;

	u_tm_log("adapter_properties_set %s:%s\n", interface, name);

	GVariant *parameters = g_variant_new("(ssv)", interface, name, value);

	g_dbus_connection_call_sync(conn,
                             	"org.bluez",
								"/org/bluez/hci1",
                             	"org.freedesktop.DBus.Properties",
                             	"Set",
                             	parameters,
                             	NULL,
                             	G_DBUS_CALL_FLAGS_NONE,
                             	-1,
                             	NULL,
                             	&error);

	if(error) {
		u_tm_log("Error: adapter_properties_set %s\n", error->message);
		g_error_free (error);
	}
	
}


static GVariant *adapter_properties_get(GDBusConnection *conn, char *interface, char *name)
{
	GError *error = NULL;
	GVariant *ret = NULL;
	
	u_tm_log("adapter_properties_get %s:%s\n", interface, name);

	GVariant *parameters = g_variant_new("(ss)", interface, name);

	GVariant *v = g_dbus_connection_call_sync(conn,
				                             	"org.bluez",
												"/org/bluez/hci1",
				                             	"org.freedesktop.DBus.Properties",
				                             	"Get",
				                             	parameters,
				                             	NULL,
				                             	G_DBUS_CALL_FLAGS_NONE,
				                             	-1,
				                             	NULL,
				                             	&error);

	if(error) {
		u_tm_log("Error: adapter_properties_get %s\n", error->message);
		g_error_free (error);
		return ret;
	}

	
	g_variant_get(v, "(v)", &ret);

	return ret;
	
}



void adapter_discoverable_enable(GDBusConnection *conn)
{
	adapter_properties_set(conn, "org.bluez.Adapter1", "Discoverable", g_variant_new("b", 1));
}

void adapter_discoverable_disable(GDBusConnection *conn)
{
	adapter_properties_set(conn, "org.bluez.Adapter1", "Discoverable", g_variant_new("b", 0));
}

void adapter_power_on(GDBusConnection *conn)
{
	adapter_properties_set(conn, "org.bluez.Adapter1", "Powered", g_variant_new("b", 1));	
}

void adapter_power_off(GDBusConnection *conn)
{
	adapter_properties_set(conn, "org.bluez.Adapter1", "Powered", g_variant_new("b", 0));
}


int adapter_power_state(GDBusConnection *conn)
{
	GVariant * v = adapter_properties_get(conn, "org.bluez.Adapter1", "Powered");
		
	int ret;
	g_variant_get(v, "b", &ret);

	return ret;
}

int adapter_discoverable_state(GDBusConnection *conn)
{
	GVariant * v = adapter_properties_get(conn, "org.bluez.Adapter1", "Discoverable");
		
	int ret;
	g_variant_get(v, "b", &ret);

	return ret;
}


