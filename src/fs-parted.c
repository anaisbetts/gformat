/*
 * fs-parted.c - Perform filesystem ops using libparted
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

#include <glib.h>
#include <glib/gi18n.h>

#include <parted/parted.h>

#include "device-info.h"
#include "fs-parted.h"

GHashTable* 
get_fs_list(void)
{
	GHashTable* ret = g_hash_table_new(g_str_hash, g_str_equal);
	PedFileSystemType* iter = ped_file_system_type_get_next(NULL);

	while(iter != NULL) {
		/* FIXME: It probably isn't kosher to go poking around in
		 * this structure, but there's no better way to do it */
		if(iter->ops->create)
			g_hash_table_insert(ret, iter->name, iter);

		iter = ped_file_system_type_get_next(iter);
	} 

	return ret;
}

PedDevice* 
get__ped(char *path)
{
  // this shouldn't be done here
  PedDevice *dev = g_new0(PedDevice, 1);
  
  dev = ped_device_get(path);
  
  if ( ped_device_open(dev) == 0)
    g_error("device %s with path %s can't be initialized ", dev->model, dev->path );
  
  g_message("device %s with path %s can be initialized ", dev->model, dev->path );

  if ( ped_device_close(dev) == 0)
    g_error("device %s with path %s can't be freed", dev->model, dev->path );

  return dev;
}    
    


