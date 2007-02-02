/*
 * device-info.c - Discover block devices
 *
 * Copyright 2007 Riccardo Setti <giskard@autistici.org>
 * 		  Paul Betts <paul.betts@gmail.com>
 *
 *
 * License:
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdio.h>
#include <string.h>

#include <libhal.h>
#include <libhal-storage.h>

#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>
#include <dbus/dbus-glib-lowlevel.h>

#include <glib.h>
#include <gdk/gdk.h>

#include "device-info.h"

void 
format_volume_free(FormatVolume* fvol)
{
	g_assert(fvol != NULL);
	libhal_volume_free(fvol->volume);
	if(fvol->icon)
		g_object_unref(fvol->icon);
	g_free(fvol);
}

static void fvlf_cb(gpointer data, gpointer dontcare) { format_volume_free(data); }
void format_volume_list_free(GSList* volume_list)
{
	g_assert(volume_list != NULL);
	g_slist_foreach(volume_list, fvlf_cb, NULL);
	g_slist_free(volume_list);
}

LibHalContext* 
libhal_context_alloc(void)
{ 
	LibHalContext *hal_ctx = NULL;
	DBusConnection *system_bus = NULL;
	DBusError error;

	hal_ctx = libhal_ctx_new ();
	if (hal_ctx == NULL) {
		g_warning ("Failed to get libhal context");
		goto error;
	}

	dbus_error_init (&error);
	system_bus = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (dbus_error_is_set (&error)) {
		g_warning ("Cannot connect to system bus: %s : %s", error.name, error.message);
		dbus_error_free (&error);
		goto error;
	}

	libhal_ctx_set_dbus_connection (hal_ctx, system_bus);

	if (!libhal_ctx_init (hal_ctx, &error)) {
		g_warning ("Failed to initialize libhal context: %s : %s", error.name, error.message);
		dbus_error_free (&error);
		goto error;
	}

	return hal_ctx;

error:
	if (hal_ctx != NULL)
		libhal_ctx_free (hal_ctx);
	return NULL;

}

GSList* 
build_volume_list(LibHalContext* ctx, enum FormatVolumeType type)
{
	const char* capability = "";
	char** device_udis;
	int i, device_udi_count = 0;
	GSList* device_list = NULL;
	DBusError error;

	switch(type) {
	case FORMATVOLUMETYPE_VOLUME:
		capability = "volume";
		break;
	case FORMATVOLUMETYPE_DRIVE:
		capability = "volume";
		break;
	}

	/* Pull the storage (or volume) list from HAL */
	dbus_error_init (&error); 
	if ( (device_udis = libhal_find_device_by_capability (ctx, capability, &device_udi_count, &error) ) == NULL) {
		LIBHAL_FREE_DBUS_ERROR (&error);
		goto out;
	}

	/* Now we use libhal-storage to get the info */
	for(i=0; i < device_udi_count; i++) {

	}
out:
	return device_list;
}
