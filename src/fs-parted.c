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

#define error_wrap(exp, err)  do { parted_clear_lasterr(); \
				   (exp); \
				   if(err && parted_get_lasterr()) \
					*err = g_error_new(0, -1, parted_get_lasterr()); \
				 } while(0)

/*
 * Structs
 */

typedef struct {
        time_t last_update;
        time_t predicted_time_left;
} TimerContext;
                   
static TimerContext timer_context;


/*
 * Utility Functions
 */

static gchar *ped_lasterr = NULL;

static void
parted_clear_lasterr(void) { if(ped_lasterr)		g_free(ped_lasterr); }

static const gchar*
parted_get_lasterr(void) { return (const gchar*)ped_lasterr; }

static PedExceptionOption
parted_exception_handler(PedException* ex)
{
	/* FIXME: This function doesn't really handle exceptions properly,
	 * it's just a crappy first attempt */
	switch(ex->type) {
	case PED_EXCEPTION_ERROR:
	case PED_EXCEPTION_FATAL:
	case PED_EXCEPTION_BUG:
	case PED_EXCEPTION_NO_FEATURE:
		parted_clear_lasterr();
		ped_lasterr = g_strdup(ex->message);
		break;

	case PED_EXCEPTION_INFORMATION:
	case PED_EXCEPTION_WARNING:
	default:
		return PED_EXCEPTION_OK;
	}

	return PED_EXCEPTION_UNHANDLED;
}

static gboolean 
parted_formatter_doformat(Formatter* this, 
			  const char* blockdev, 
			  const char* fs, 
			  int partition_number,
			  GHashTable* options, 
			  GError** error)
{
	PedDevice* dev;
        PedDisk* disk;
	PedGeometry fs_geometry;
	PedPartition *part = NULL;
	const PedFileSystemType* fs_type = NULL;
	gboolean ret = FALSE;

        error_wrap( dev = ped_device_get(blockdev), error );
        if (!dev) 	goto out;

	error_wrap( disk = ped_disk_new(dev), error );
        if (!disk) 	goto out;

	error_wrap( fs_type = ped_file_system_type_get (fs), error );
	g_assert(fs_type != NULL);
	if (!fs_type)	goto out_destroy_disk;

	if(partition_number != FORMATTER_DONT_SET_PARTITION) {
		/* XXX: Do we have to free this? It appears to be part of the
		 * disk */
		part = ped_disk_get_partition (disk, partition_number);
		memcpy(&fs_geometry, &part->geom, sizeof(PedGeometry));
	}
	else {
		/* We have to manually create a geometry definition for devices
		 * that have no partition table */
		fs_geometry.dev = dev;
		fs_geometry.length = dev->length;
		fs_geometry.start = 0;	fs_geometry.end = dev->length - 1;
	}

	/* Actually do the format here */
	/* We don't care about the timer because creating filesystems takes a
	 * trivial amount of time */
	PedFileSystem* pfs;
	formatter_client_set_text(this, _("Creating file system"));
	formatter_client_set_progress(this, 0.33);
	error_wrap( pfs = ped_file_system_create (&fs_geometry, fs_type, NULL), error );
	if (!pfs) 	goto out_destroy_disk;
	ped_file_system_close (pfs);         

	/* Fix the partition table */
	ret = TRUE;
	formatter_client_set_text(this, _("Setting partition table"));
	formatter_client_set_progress(this, 0.66);
	if(partition_number != FORMATTER_DONT_SET_PARTITION)
		error_wrap( ret = (gboolean)ped_partition_set_system (part, fs_type), error );

	formatter_client_set_progress(this, 1.0);

        out_destroy_disk:
                ped_disk_destroy (disk);
        out:
                return ret;
}

static void 
parted_formatter_unref(Formatter* this)
{
	g_free(this->available_fs_list);
}


/*
 * Public Functions
 */

Formatter*
parted_formatter_init(void)
{
	/* FIXME: There's got to be a less sloppy way to do this */
	const char* fs_list[128];
	int fs_list_count = 0;
	Formatter* ret = g_new0(Formatter, 1);
	const FormatterOps fops = { NULL /*parted_formatter_canformat*/, parted_formatter_doformat, parted_formatter_unref};

	/* Note: This isn't documented in the libparted refs; to get the first 
	 * filesystem in the list of supported filesystems, you pass NULL as 
	 * the param to this function */
	PedFileSystemType* iter = ped_file_system_type_get_next(NULL);

	/* Figure out the list of supported filesystems */
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

	/* Initialize the formatter */
	ped_exception_set_handler(parted_exception_handler);
	ret->available_fs_list = (const char**)g_new0(char*, fs_list_count + 1);
	memcpy(ret->available_fs_list, fs_list, sizeof(char*) * fs_list_count);
	ret->fops = fops;

	return ret;
}

static void
timer_handler (PedTimer *timer, void *ctx)
{
#if 0
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
#endif
        fprintf(stdout, ".\n");
        fflush(stdout);
}
 
