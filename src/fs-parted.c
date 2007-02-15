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
#include "formatterbase.h"
#include "fs-parted.h"

#if 0
/* TODO: We should do a more thorough check to see if we really can
 * format this device using this filesystem
 */
static gboolean 
parted_formatter_canformat(Formatter* this, const char* blockdev, const char* fs)
{
}
#endif

typedef struct {
        time_t last_update;
        time_t predicted_time_left;
} TimerContext;
                   
static TimerContext timer_context;

static gboolean 
parted_formatter_doformat(Formatter* this, const char* blockdev, const char* fs, GHashTable* options, GError** error)
{
}

static void 
parted_formatter_unref(Formatter* this)
{
	g_free(this->available_fs_list);
}

Formatter*
parted_formatter_init(void)
{
	/* FIXME: There's got to be a less sloppy way to do this */
	const char* fs_list[128];
	int fs_list_count = 0;
	Formatter* ret = g_new0(Formatter, 1);
	const FormatterOps fops = { NULL /*parted_formatter_canformat*/, parted_formatter_doformat, parted_formatter_unref};

	PedFileSystemType* iter = ped_file_system_type_get_next(NULL);

	while(iter != NULL) {
		/* FIXME: It probably isn't kosher to go poking around in
		 * this structure, but there's no better way to do it */
		if(iter->ops->create) {
			fs_list[fs_list_count] = iter->name;
			fs_list_count++;
		}
		iter = ped_file_system_type_get_next(iter);
	}

	if(fs_list_count == 0)
		return NULL;

	ret->available_fs_list = (const char**)g_new0(char*, fs_list_count + 1);
	memcpy(ret->available_fs_list, fs_list, sizeof(char*) * fs_list_count);
	ret->fops = fops;

	return ret;
}

static void
timer_handler (PedTimer *timer, void *ctx)
{

                int draw_this_time;
                TimerContext* tctx = (TimerContext*) ctx;

        if (tctx->last_update != timer->now && timer->now > timer->start)
        {
	        tctx->predicted_time_left = timer->predicted_end - timer->now;
	        tctx->last_update = timer->now;
	        draw_this_time = 1;
        }
        else
	        draw_this_time = 0;

        if (draw_this_time)
        {
                printf("\r                                                            \r");
	if (timer->state_name)
                printf("%s... ", timer->state_name);
	
        printf("%0.f%%\t(time left %.2ld:%.2ld)",
		100.0 * timer->frac,
		tctx->predicted_time_left / 60,
		tctx->predicted_time_left % 60);

	fflush(stdout);
    }

        fprintf(stdout, ".\n");
        fflush(stdout);
}
 
int
do_operations(char *path)
{
        PedDevice *dev = ped_device_get(path);
        printf("device %s with path %s can be initialized \n", dev->model, dev->path );
       
        PedDisk *disk = ped_disk_new(dev);
        if (!disk)
                 goto error;
        printf("ped_disk end\n");
       
        /*
        PedSector  value = 0;
        PedGeometry *geom = ped_geometry_new( dev, &value, 1);
        */
        
        /*for /dev/sdb1 */
        PedPartition  *part = ped_disk_get_partition (disk, 1);
        printf("ped_partition end\n");
 
        const PedFileSystemType *type = ped_file_system_type_get ("fat16") ;
        
        PedTimer *timer = NULL;
        //timer = ped_timer_new (timer_handler, &timer_context);
 
        printf("ped_filesystem start\n");
        PedFileSystem *fs = ped_file_system_create (&part->geom, type, timer);
        printf("ped_filesystem end\n");
        
        ped_partition_set_system (part, type);

        ped_disk_print(disk);
        
        error:
                return 0;
}
