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
get_ped(char *path)
{
        PedDevice *dev = ped_device_get(path);
        
        printf("device %s with path %s can be initialized \n", dev->model, dev->path );
       
        return dev;
       
}    
    

PedDisk *
get_partition_table(PedDevice *dev)
{
      
      PedDisk *disk = ped_disk_new(dev);
      
      return disk;

}

/*
PedGeometry *
get_ped_geometry(PedDevice *dev){
        
        PedSector  value = 0; 

        PedGeometry *geom = ped_geometry_new( dev, &value, 1);

        return geom;
}
*/

void 
list_partitions(PedDisk *disk)
{
        ped_disk_print(disk);
}

void
do_operations(char *path)
{
        PedDevice *dev = get_ped(path);
        PedDisk  *disk = get_partition_table(dev);

        list_partitions(disk);
        
}
