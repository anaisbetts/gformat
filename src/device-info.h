/*
 * device-info.h - Discover block devices
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

#ifndef _DEVICE_INFO_H
#define _DEVICE_INFO_H

#include <glib.h>
#include <glib/gi18n.h>
#include <gdk/gdk.h>

#include <libhal.h>
#include <libhal-storage.h>

#include "partutil.h"

typedef struct FormatVolume FormatVolume;

/* Some explanation is in order: Since it's plausible that one would want to
 * format either a "drive" as a whole or an individual partition, we include
 * the field for both of them, but only one should be non-null. Volume trumps 
 * drive; if volume is non-NULL, the structure represents a volume. Otherwise
 * it represents a drive which may or may not have a disk in it */
struct FormatVolume {
	LibHalVolume *volume;		
	gchar* drive_udi;		/* The udi of the drive containing
					   this volume; may be NULL */

	LibHalDrive *drive;
	gchar* udi;
	gchar* mountpoint;

	GdkPixbuf* icon;
	gchar* friendly_name;
};

enum FormatVolumeType {
	FORMATVOLUMETYPE_DRIVE,		/* Includes drives with no disks */
	FORMATVOLUMETYPE_VOLUME,	/* Includes partitions */
};

void format_volume_free(FormatVolume* volume);
void format_volume_list_free(GSList* volume_list);

guint64 get_format_volume_size(const FormatVolume* vol);
gchar* get_friendly_drive_name(LibHalDrive* drive);
gchar* get_friendly_drive_info(LibHalDrive* drive);
gchar* get_friendly_volume_name(LibHalContext* ctx, LibHalVolume* volume);
gchar* get_friendly_volume_info(LibHalContext* ctx, LibHalVolume* volume);

GHashTable* create_icon_cache(void);
gboolean write_partition_table_for_device(LibHalDrive* drive, PartitionScheme scheme, GError** error);
GSList* get_volumes_mounted_on_drive(LibHalContext* ctx, LibHalDrive* drive);
GSList* build_volume_list(LibHalContext* ctx, 
		  enum FormatVolumeType type, 
		  GHashTable* icon_cache, 
		  int icon_width, int icon_height);
LibHalContext* libhal_context_alloc(void);

#endif
