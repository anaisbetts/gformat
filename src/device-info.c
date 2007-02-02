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
	if(fvol->friendly_name)
		g_free(fvol->friendly_name);
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

static GdkPixbuf* 
load_icon_from_cache(const char* path, GHashTable* icon_cache, int width, int height)
{
	GdkPixbuf* ret;

	if(!path) 	return NULL;
	if (icon_cache && (ret = g_hash_table_lookup(icon_cache, path)) )
		return ret;

	GError* err = NULL;
	if(width && height) {
		ret = gdk_pixbuf_new_from_file_at_size(path, width, height, &err);
	}
	else {
		ret = gdk_pixbuf_new_from_file(path, &err);
	}
	if(err) {
		g_warning("Couldn't load icon '%s'! message = '%s'", path, err->message);
		g_error_free(err);
	}

	if(icon_cache)
		g_hash_table_insert(icon_cache, g_strdup(path), ret);

	return ret;
}

static void mem_free_cb(gpointer data)  { g_free(data); }
static void unref_free_cb(gpointer data)  { g_object_unref( G_OBJECT(data) ); }

GHashTable* create_icon_cache(void)
{
	return g_hash_table_new_full(g_str_hash, g_str_equal, mem_free_cb, unref_free_cb);
}

GSList* 
build_volume_list(LibHalContext* ctx, 
		  enum FormatVolumeType type, 
		  GHashTable* icon_cache, 
		  int icon_width, int icon_height)
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
		capability = "storage";
		break;
	}

	/* Pull the storage (or volume) list from HAL */
	dbus_error_init (&error); 
	if ( (device_udis = libhal_find_device_by_capability (ctx, capability, &device_udi_count, &error) ) == NULL) {
		LIBHAL_FREE_DBUS_ERROR (&error);
		goto out;
	}

	/* Now we use libhal-storage to get the info */
	FormatVolume* current;
	const char* icon_path;
	for(i=0; i < device_udi_count; i++) {
		current = g_new0(FormatVolume, 1);

		g_warning("udi: %s", device_udis[i]);
		switch(type) {
		case FORMATVOLUMETYPE_VOLUME:
			current->volume = libhal_volume_from_udi(ctx, device_udis[i]);
			if(!current->volume) {
				g_free(current);
				continue;
			}

			/* FIXME: This tastes like wrong */
			current->icon = NULL;

			/* FIXME: Fill in the actual friendly name */
			current->friendly_name = g_strdup(libhal_volume_get_uuid(current->volume));
			break;

		case FORMATVOLUMETYPE_DRIVE:
			current->drive = libhal_drive_from_udi(ctx, device_udis[i]);
			if(!current->drive) {
				g_free(current);
				continue;
			}

			g_warning("Icon drive: %s; Icon volume: %s",
					libhal_drive_get_dedicated_icon_drive(current->drive),
					libhal_drive_get_dedicated_icon_volume(current->drive));
			icon_path = libhal_drive_get_dedicated_icon_drive(current->drive);
			current->icon = load_icon_from_cache(icon_path, icon_cache, icon_width, icon_height);

			/* FIXME: Fill in the actual friendly name */
			current->friendly_name = g_strdup(libhal_drive_get_serial(current->drive));

			break;
		}

		/* Do some last minute sanity checks */
		if(!current->friendly_name)	current->friendly_name = "";

		device_list = g_slist_prepend(device_list, current);
	}
	
	if(device_udis)
		libhal_free_string_array(device_udis);

out:
	return device_list;
}
