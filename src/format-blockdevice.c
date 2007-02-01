/*
 * gnome-format-blockdevice.c - discover block device
 *
 * Copyright 2007 Riccardo Setti <giskard@autistici.org>
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

#include "format-blockdevice.h"

LibHalContext *
new_hal_ctx () 
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

extern void 
find_device_infos (FormatBlockDevice *device, char *udi)
{
  
  device->volume = libhal_volume_from_udi (device->hal_ctx, udi);

}



extern FormatBlockDevice * 
find_volume ()
{
  int i;
  int num_hal_udis;
  DBusError error;

  FormatBlockDevice *result = g_malloc0(sizeof(FormatBlockDevice));
  
  result->hal_ctx = new_hal_ctx();
  
  dbus_error_init (&error); 
  if ((result->udis = libhal_find_device_by_capability (result->hal_ctx, "volume", &num_hal_udis, 
                  &error)) == NULL) {
      LIBHAL_FREE_DBUS_ERROR (&error);
  }

  result->num_udis = num_hal_udis;
  
  return result;

}


