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
#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "device-info.h"
#include "partutil.h"

void 
format_volume_free(FormatVolume* fvol)
{
	g_assert(fvol != NULL);

	if(fvol->volume)
		libhal_volume_free(fvol->volume);
	if(fvol->drive)
		libhal_drive_free(fvol->drive);
	if(fvol->icon)
		g_object_unref(fvol->icon);
	if(fvol->friendly_name)
		g_free(fvol->friendly_name);
	if(fvol->udi)
		g_free(fvol->udi);
	if(fvol->drive_udi)
		g_free(fvol->drive_udi);
	if(fvol->mountpoint)
		g_free(fvol->mountpoint);
		
	g_free(fvol);
}

static void fvlf_cb(gpointer data, gpointer dontcare) { format_volume_free(data); }
void format_volume_list_free(GSList* volume_list)
{
	g_assert(volume_list != NULL);
	g_slist_foreach(volume_list, fvlf_cb, NULL);
	g_slist_free(volume_list);
}

/* We should really change the name of this func*/
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

guint64 
get_format_volume_size(const FormatVolume* vol)
{
	g_assert(vol != NULL);

	if(vol->volume)
		return libhal_volume_get_size(vol->volume);

	if(vol->drive && libhal_drive_is_media_detected(vol->drive))
		return libhal_drive_get_media_size(vol->drive);

	if(vol->drive)
		return libhal_drive_get_size(vol->drive);

	return 0;
}

char*
get_friendly_drive_name(LibHalDrive* drive)
{
	const char* ret;

	/* Getting the name is tricky because we never know what info we have
	 * so we have to fall-back all the time */
	ret = libhal_drive_get_model(drive);
	if(ret && ret[0] != 0)	goto out;

	ret = libhal_drive_get_vendor(drive);
	if(ret && ret[0] != 0)	goto out;

	ret = libhal_drive_get_device_file(drive);
	if(ret && ret[0] != 0)	goto out;

	ret = _("(Unknown Device)");

out:
	return g_strdup(ret);
}

gchar*
get_friendly_drive_info(LibHalDrive* drive)
{
	const char* device_name;
	gchar* friendly_size, *ret = NULL;
	dbus_uint64_t size;

	size = (libhal_drive_uses_removable_media(drive) ?
			libhal_drive_get_media_size(drive) :
			libhal_drive_get_size(drive));
	friendly_size = (size > 0 ? 
			gnome_vfs_format_file_size_for_display((GnomeVFSFileSize)size) :
			NULL);

	device_name = get_friendly_drive_name(drive);

	if(friendly_size) {
		ret = g_strdup_printf("%s - %s", device_name, friendly_size);
		g_free(friendly_size);
	}
	else
		ret = g_strdup(device_name);

	return ret;
}

char*
get_friendly_volume_name(LibHalContext* ctx, LibHalVolume* volume)
{
	char* ret, *tmp;
	char* partition_name = NULL;
	int partition_num;

	ret = libhal_volume_get_label(volume);
	if(ret && ret[0] != 0)	return g_strdup(ret);

	/* Try to describe the device */
	const char* assoc_udi = libhal_volume_get_storage_device_udi(volume);

	LibHalDrive* assoc_drv = NULL;
	if(assoc_udi) 	assoc_drv = libhal_drive_from_udi(ctx, assoc_udi);
	if(assoc_drv) {
		partition_num = (libhal_volume_is_partition(volume) ? 
				(int)libhal_volume_get_partition_number(volume) :
				-1);

		if(partition_num > 0) {
			/* Try to get the name */
			tmp = libhal_volume_get_partition_label(volume);
			if(tmp && tmp[0] != 0)
				partition_name = g_strdup(tmp);
			else
				partition_name = g_strdup_printf(_("Partition %d"), partition_num);
		}

		/* if device is partition we can tell where it's actually mounted */
		tmp = get_friendly_drive_name(assoc_drv);
		if(partition_name) {
			ret = g_strdup_printf(_("%s on %s"), partition_name, tmp);
			g_free(partition_name);
		}
		else {
			ret = g_strdup_printf(_("(Unknown Volume) on %s"), tmp);
		}
		g_free(tmp);
		libhal_drive_free(assoc_drv);
		return ret;
	}

	ret = _("(Unknown Volume)");

	return g_strdup(ret);
}

gchar*
get_friendly_volume_info(LibHalContext* ctx, LibHalVolume* volume)
{
	char* device_name;
	gchar* friendly_size, *ret = NULL;
	dbus_uint64_t size;

	size = libhal_volume_get_size(volume);
	friendly_size = (size > 0 ? 
			gnome_vfs_format_file_size_for_display((GnomeVFSFileSize)size) :
			NULL);

	device_name = get_friendly_volume_name(ctx, volume);

	if(friendly_size) {
		ret = g_strdup_printf("%s - %s", device_name, friendly_size);
		g_free(friendly_size);
		g_free(device_name);
	}
	else
		ret = device_name;

	return ret;
}


/* from <linux/ioctl.h> */
#define _IOC_NONE	0U
#define _IOC_WRITE	1U
#define _IOC_READ	2U
#define _IOC(dir,type,nr,size) \
	(((dir)  << _IOC_DIRSHIFT) | \
	 ((type) << _IOC_TYPESHIFT) | \
	 ((nr)   << _IOC_NRSHIFT) | \
	 ((size) << _IOC_SIZESHIFT))

/* from <linux/fs.h> */
#define _IO(type,nr)		_IOC(_IOC_NONE,(type),(nr),0)
#define BLKRRPART  _IO(0x12,95) /* re-read partition table */

gboolean
repoll_partition_table_linux(const char* dev)
{
	int fd = open(dev, O_RDWR);
	int retry_count = 5;
	if(fd < 1)	return FALSE;
sync();
        while (ioctl (fd, BLKRRPART)) {
                retry_count--;
                sync();
                if (!retry_count)
			goto out;
        }

	/* Wait awhile for the kernel to repoll */
	sleep(1);

out:
	close(fd);
	return TRUE;
}

gboolean
repoll_partition_table(const char* dev)
{
	/* FIXME: We need to dispatch to other places for Solaris
	 * and BSD */
	return repoll_partition_table_linux(dev);
}

gboolean
write_partition_table_for_device(LibHalDrive* drive, PartitionScheme scheme, GError** error)
{
	const char* msg;
	g_assert(drive);

	char* dev = libhal_drive_get_device_file(drive);
	g_assert(dev);
	if(!dev)	return FALSE;

	/* Create a new table first */
	if(!part_create_partition_table(dev, scheme)) {
		msg =  _("Cannot create partition table on %s");
		goto error_out;
	}

	/* Create one partition in this table; we start the FS at sector
	 * 63 */
	const int start = 512*63;
	guint64 size = (guint64)libhal_drive_get_size(drive);
	if(size < start) {
		msg = _("Cannot create partition table on %s");
		goto error_out;
	}

	/* This doesn't matter, we're going to reset it later;
	 * we just need something to give to part_add_partition */
	const char* type;
	switch(scheme) {
	case PART_TYPE_GPT:
		type = "{}"; /* FIXME: Some lame GUID */
		break;
	case PART_TYPE_APPLE:
		type = "DOS_FAT_32";
		break;
	default:
		type = "0x83";
	}

	guint64 dontcare;
	if(!part_add_partition(dev, start, size, &dontcare, &dontcare, 
			       (char*)type, NULL, NULL, 0, 0)) {
		msg = _("Cannot add new partition on %s");
		goto error_out;
	}

	if(!repoll_partition_table(dev)) {
		msg = _("The kernel cannot repoll the partition table on %s. "
			"Please reboot the computer or reinsert this disk if it "
			"is removable and try again.");
		goto error_out;
	}

	return TRUE;

error_out:
	char* name = get_friendly_drive_name(drive);
	g_set_error(error, 0, 0, _("Cannot create partition table on %s"), name);
	g_free(name);
	return FALSE;
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

	/* if we use libhal_device_get-property() instead of 
	 * libhal_volume_get_mount_mount_point() we have to setup DBusError and
	 * some other things, the problem is do we like to have (null) in the gui
	 * when libhal-storage cannot discover where a partition is mounted?
	 * if so we can remove the DBusError code and use the libhal-storage func
	 * to retrive the mount point.
	 * (or maybe we can check if is_partition == null and then don't strdup
	 * it.
	 * It returns null when a device is marked as swap or mounted via
	 * cryptdisk or even if it's not listed in /etc/fstab
	 */

	/* Now we use libhal-storage to get the info */
	FormatVolume* current;
	const char* icon_path;
	for(i=0; i < device_udi_count; i++) {
		current = g_new0(FormatVolume, 1);

		g_warning("udi: %s", device_udis[i]);
		current->udi = g_strdup(device_udis[i]);
		switch(type) {
		case FORMATVOLUMETYPE_VOLUME:
			current->volume = libhal_volume_from_udi(ctx, device_udis[i]);
			if(!current->volume) {
				g_free(current);
				continue;
			}

			/* FIXME: This tastes like wrong */
			current->icon = NULL;

			current->friendly_name = get_friendly_volume_info(ctx, current->volume);
			current->drive_udi = g_strdup(libhal_volume_get_storage_device_udi(current->volume));
			current->mountpoint = g_strdup(libhal_volume_get_mount_point(current->volume));
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

			current->friendly_name = get_friendly_drive_info(current->drive);

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
