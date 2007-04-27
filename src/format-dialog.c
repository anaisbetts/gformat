/*
 * format-dialog.c - initialize dialog
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomeui/gnome-help.h>
#include <libgnomeui/gnome-ui-init.h>
#include <glade/glade.h>

#include <libhal.h>
#include <libhal-storage.h>

#include "device-info.h"
#include "formattify.h"
#include "format-dialog.h"

enum {
	DEV_COLUMN_UDI = 0,
	DEV_COLUMN_ICON,
	DEV_COLUMN_NAME_MARKUP,
	DEV_COLUMN_SENSITIVE,
};

enum {
	FS_COLUMN_REALNAME = 0,
	FS_COLUMN_MARKUP,
	FS_COLUMN_SENSITIVE,
};

#define LUKS_HAL_MIN_VERSION 	581 		/* 0.5.8.1 */
#define LUKS_BLKDEV_MIN_SIZE 	(1048576 << 4) 	/* At least 16M */


/*
 * Utility Functions
 */

static void
show_error_dialog (GtkWidget *parent, gchar *main, gchar *secondary)
{
	GtkWidget *dialog;
	GtkDialogFlags flags;
	
	if (parent != NULL)
		flags = GTK_DIALOG_MODAL;
	
	dialog = gtk_message_dialog_new_with_markup (GTK_WINDOW(parent), flags, 
						     GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
						     "<span weight=\"bold\" size=\"larger\">%s</span>\n\n%s",
						     main, secondary);
	gtk_window_set_title (GTK_WINDOW (dialog), "");
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}

static const gchar*
get_fs_from_menuitem_name(const gchar* menuitem_name)
{
	const gchar* ret;
	for(ret = menuitem_name + (strlen(menuitem_name) - 1); ret > menuitem_name; ret--)
	{
		if(*ret == '_')
			return ret+1;
	}
	return ret;
}

static const gchar*
get_fs_from_menu(FormatDialog* dialog)
{
	return "ext2";		/* FIXME: Figure out how to snag the current menu item */
}

gchar*
get_udi_from_iter(FormatDialog* dialog, GtkTreeIter* iter)
{
	char* ret;
	GValue val = {0, };
	gtk_tree_model_get_value(GTK_TREE_MODEL(dialog->volume_model), iter, DEV_COLUMN_UDI, &val);
	if(val.g_type == 0)
		return NULL;

	ret = g_strdup(g_value_get_string(&val));
	g_value_unset(&val);
	return ret;
}

static gboolean
hal_cb(gpointer data)
{
	gint hal_version = 0;
	gchar** split = NULL;
	g_assert(data != NULL);

	ProcessOutput* output = data;
	FormatDialog* dialog = output->user_data;
	g_assert(dialog != NULL);
	if(!output->stdout_output)
		goto out;

	split = g_strsplit_set(output->stderr_output, " .", 0 /*All tokens*/);

	if(!split || !split[0])
		goto out;

	/* FIXME: Make this check more thorough */
	if(!split[3] || !split[4] || !split[5] || !split[6])
		goto out;
	hal_version = atoi(split[3])*1000 + atoi(split[4])*100 + atoi(split[5])*10 + atoi(split[6]);
	
out:
	if(split)
		g_strfreev(split);
	process_output_free(output);
	dialog->hal_version = hal_version;
	return FALSE;
}

void
get_hal_version(FormatDialog* dialog)
{
	/* FIXME: This is hardcoded. This is bad. */
	const char* cmd[] = {"/usr/sbin/hald", "--version"};
	if(!spawn_async_get_output(cmd, hal_cb, dialog))
		dialog->hal_version = 0;
}

static gboolean
luks_valid_for_device (const FormatVolume* dev)
{
	/* AFAIK, you can format LUKS on anything that is above a certain size */
	/* FIXME: Add in the check for HAL as well */
	g_assert(dev != NULL);

	return (get_format_volume_size(dev) >= LUKS_BLKDEV_MIN_SIZE);
}

static gboolean
floppy_valid_for_device (const FormatVolume* dev)
{
	g_assert(dev != NULL);
	if(dev->drive)
		return (libhal_drive_get_type(dev->drive) == LIBHAL_DRIVE_TYPE_FLOPPY);

	return FALSE;
}

static gboolean
formatter_handle_error(gpointer data)
{
	fmt_thread_params* params = data;
	gchar* pri_msg = g_strdup_printf(_("There was an error formatting %s"), params->vol->friendly_name);
	show_error_dialog(params->dialog->toplevel, pri_msg, params->error->message);

	g_error_free(params->error);
	g_free(params);
	return FALSE;
}

static const FormatVolume* 
get_cached_device_from_udi(FormatDialog* dialog, const char* udi)
{
	gboolean is_drive_list = TRUE;
	GSList* iter = dialog->hal_drive_list;

	while(iter) {
		FormatVolume* current = iter->data;

		if(current == NULL) {
			g_error("current volume is null?");
			continue;
		}

		if(!strcmp(current->udi, udi))
			return current;

		iter = g_slist_next(iter);
		if(!iter && is_drive_list) {
			iter = dialog->hal_volume_list;
			is_drive_list = FALSE;
		}
	}

	return NULL;
}

const FormatVolume*
get_cached_device_from_treeiter(FormatDialog* dialog, GtkTreeIter* iter)
{
	gchar* udi = get_udi_from_iter(dialog, iter);
	if(!udi) {
		g_warning("UDI for iter was null!");
		return NULL;
	}

	const FormatVolume* ret = get_cached_device_from_udi(dialog, udi);

	g_free(udi);	return ret;
}

gboolean
warn_user_of_impending_doom(FormatDialog* dialog, FormatVolume* target)
{
	GSList* mounted_list = NULL;

	/* Figure out if we're about to run over any live partitions first */
	if(target->volume) {
		if(libhal_volume_is_mounted(target->volume)) {
			mounted_list = g_slist_prepend(mounted_list, 
						       get_friendly_volume_name(dialog->hal_context, target->volume));
		}
	}
	else {
		mounted_list = get_volumes_mounted_on_drive(dialog->hal_context, target->drive);
	}

	gchar* message;
	gchar* name = (target->volume ? get_friendly_volume_name(dialog->hal_context, target->volume) : 
					get_friendly_drive_name(target->drive));
	/* Come up with the error message */
	if(!mounted_list) {
		message = g_strdup_printf(_("Formatting will irreversibly destroy all data on %s. "
					    "Are you sure you want to continue?"), name);
	}
	else {
		/* FIXME: There are a ton of malloc's here */
		int i; 	GSList* iter;
		gchar* tmp_list[65];

		/* TODO: It'd be cool if these were hyperlinks that opened nautilus at the mountpoint! */
		for(iter = mounted_list, i=0; iter != NULL && i < 64; iter = iter->next, i++) {
			FormatVolume* current = get_cached_device_from_udi(dialog, (char*)iter->data);
			g_debug("Mounted: %s", iter->data);
			if(!current) 	continue;

			gchar* mountpoint = libhal_volume_get_mount_point(current->volume);
			tmp_list[i] = g_strdup_printf( _("%s mounted at %s"), current->friendly_name, mountpoint);
		}
		tmp_list[i] = NULL;

		gchar* vol_list = g_strjoinv("\n", tmp_list);

		message = g_strdup_printf( _("Formatting will irreversibly destroy all data on %s, "
					     "including these volumes currently in use:"
					     "\n\n%s\n\n"
					     "Are you sure you want to continue?"), name, vol_list);

		/* Free our stuff */
		g_free(vol_list);
		for(int i=0; i < 64; i++) {
			if(!tmp_list[i]) break;
			g_free(tmp_list[i]);
		}
	}
	g_free(name);

	/* Show the dialog and get the response back */
	GtkWidget* messagebox;
	messagebox = gtk_message_dialog_new(GTK_WINDOW(dialog->toplevel), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, _("Formatting will erase data"));
	gtk_message_dialog_format_secondary_markup(GTK_MESSAGE_DIALOG(messagebox), message);
	gtk_dialog_add_buttons(GTK_DIALOG(messagebox), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, 
			 _("Format device"), GTK_RESPONSE_OK, NULL);

	int id = gtk_dialog_run(GTK_DIALOG(messagebox));
	gtk_widget_destroy(messagebox);

	return (id == GTK_RESPONSE_OK);
}


/*
 * High-level functions (aka 'big' functions)
 */

static void 
setup_volume_treeview (FormatDialog* dialog)
{
	GtkTreeStore* model;
	GtkCellRenderer* icon_renderer;
	GtkCellRenderer* text_renderer;
	GtkComboBox* combo = dialog->volume_combo;

	/* udi, icon, name, sensitive */
	model = gtk_tree_store_new(4, G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_BOOLEAN);
	gtk_combo_box_set_model(combo, GTK_TREE_MODEL(model));

	/* Set up the column */
	gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(combo), (icon_renderer = gtk_cell_renderer_pixbuf_new()), FALSE /* expand? */);
	gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(combo), (text_renderer = gtk_cell_renderer_text_new()), TRUE );
	gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(combo), icon_renderer, "pixbuf", DEV_COLUMN_ICON );
	gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(combo), icon_renderer, "sensitive", DEV_COLUMN_SENSITIVE );
	gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(combo), text_renderer, "markup", DEV_COLUMN_NAME_MARKUP );
	gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(combo), text_renderer, "sensitive", DEV_COLUMN_SENSITIVE );

	/* Do some miscellaneous things */
	dialog->icon_cache = create_icon_cache();
	dialog->volume_model = model;
}

struct _setup_fs_duple {
	GtkTreeStore* model;
	GtkTreeIter* parent;
};

static void
setup_fs_cb(gpointer key, gpointer value, gpointer user_data)
{
	gchar* current_fs = key;
	struct _setup_fs_duple* s = user_data;

	gtk_tree_store_insert_with_values(s->model, NULL, s->parent, 100 /*ditto*/,
		FS_COLUMN_REALNAME, current_fs,
		FS_COLUMN_MARKUP, current_fs,
		FS_COLUMN_SENSITIVE, FALSE, -1); /* We update this later */
}

static void
setup_filesystem_menu(FormatDialog* dialog)
{
	GtkTreeStore* model;
	model = gtk_tree_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
	GtkComboBox* combo = dialog->fs_combo;
	GtkCellRenderer* text_renderer;
	gtk_combo_box_set_model(combo, GTK_TREE_MODEL(model));

	/* Set up the column */
	gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(combo), (text_renderer = gtk_cell_renderer_text_new()), TRUE );
	gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(combo), text_renderer, "markup", FS_COLUMN_MARKUP );
	gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(combo), text_renderer, "sensitive", FS_COLUMN_SENSITIVE );

	/* Add the default items */
	gtk_tree_store_insert_with_values(model, NULL, NULL, 100 /*Always at end*/,
			FS_COLUMN_REALNAME, "friendly_vfat",
			FS_COLUMN_MARKUP, _("For all computers"),
			FS_COLUMN_SENSITIVE, TRUE, -1);

	gtk_tree_store_insert_with_values(model, NULL, NULL, 100 /*Always at end*/,
			FS_COLUMN_REALNAME, "friendly_ext2",
			FS_COLUMN_MARKUP, _("For Linux computers"),
			FS_COLUMN_SENSITIVE, TRUE, -1);

	gtk_tree_store_insert_with_values(model, NULL, NULL, 100 /*Always at end*/,
			FS_COLUMN_REALNAME, "friendly_hfsplus",
			FS_COLUMN_MARKUP, _("For Apple computers"),
			FS_COLUMN_SENSITIVE, TRUE, -1);

	GtkTreeIter parent = {0, NULL}; 
	gtk_tree_store_insert_with_values(model, &parent, NULL, 100 /*Always at end*/,
			FS_COLUMN_REALNAME, "specific_fs",
			FS_COLUMN_MARKUP, _("Specific Filesystem"),
			FS_COLUMN_SENSITIVE, TRUE, -1);

	/* Populate the specific fs list */
	struct _setup_fs_duple s;
	s.model = model; 	s.parent = &parent;
	g_hash_table_foreach(dialog->fs_map, setup_fs_cb, &s);
}

static gboolean 
update_device_lists(FormatDialog* dialog)
{
	if(dialog->hal_drive_list) {
		format_volume_list_free(dialog->hal_drive_list);
		dialog->hal_drive_list = NULL;
	}

	dialog->hal_drive_list = build_volume_list(dialog->hal_context, FORMATVOLUMETYPE_DRIVE,
			dialog->icon_cache, 22, 22);

	if(!dialog->hal_drive_list) {
		show_error_dialog(dialog->toplevel, 
				_("Cannot get list of disks"), 
				_("Make sure the HAL daemon is running and configured correctly"));
		return FALSE;
	}

	if(dialog->hal_volume_list) {
		format_volume_list_free(dialog->hal_volume_list);
		dialog->hal_volume_list = NULL;
	}

	dialog->hal_volume_list = build_volume_list(dialog->hal_context, FORMATVOLUMETYPE_VOLUME,
			dialog->icon_cache, 22, 22);

	if(!dialog->hal_volume_list) {
		show_error_dialog(dialog->toplevel, 
				_("Cannot get list of partitions"), 
				_("Make sure the HAL daemon is running and configured correctly"));
		return FALSE;
	}

	return TRUE;
}

static FormatVolume*
write_partition_table(FormatDialog* dialog, FormatVolume* vol, const char* fs)
{
	FormatVolume* ret = NULL;
	char* drive_udi = g_strdup(libhal_drive_get_udi(vol->drive));

	/* Write out a new table */
	GError* err = NULL;
	const char* dev = libhal_drive_get_device_file(vol->drive);
	if(!write_partition_table_for_device(vol->drive, formatter_table_hint(vol->drive, dev, fs), &err)) {
		show_error_dialog(dialog->toplevel, _("Error formatting disk"), err->message);
		g_error_free(err);
		goto out;
	}

	/* Find the partition attached to our drive */
	if(!update_device_lists(dialog))	goto out;
	GSList* iter; 
	for(iter = dialog->hal_volume_list; iter != NULL; iter = g_slist_next(iter)) {
		FormatVolume* vol = iter->data;
		if(!strcmp(vol->drive_udi, drive_udi))	break;
	}
	if(iter == NULL) {
		show_error_dialog(dialog->toplevel, _("Error formatting disk"), 
				_("Can't find new partition after formatting. Try again"));
		goto out;
	}

	ret = iter->data;

out:
	return ret;
}

static void
rebuild_volume_combo(FormatDialog* dialog)
{
	g_assert(dialog && dialog->volume_model);
	gboolean show_partitions = gtk_toggle_button_get_active(dialog->show_partitions);
	if( !update_device_lists(dialog) )
		return;

	/* Iterate through the volume list and build the tree model. If we're
	 * listing partitions, it's a bit trickier: first we add all the drives,
	 * and make a udi=>GtkTreeIter table. Then we use that table to add
	 * the partitions to the correct drives */
	GSList* treeiter_list = NULL, *device_list = dialog->hal_drive_list;
	GSList* iter;	gboolean not_empty = FALSE, can_format, listed_drives = FALSE;
	GHashTable* udi_table = g_hash_table_new(g_str_hash, g_str_equal);
	FormatVolume* current;
	GtkTreeIter* parent_treeiter, *current_treeiter;

	gtk_tree_store_clear(dialog->volume_model);
	while(device_list != NULL) {
		for(iter = device_list; iter != NULL; iter=iter->next) {
			current = iter->data;
			can_format = (current->volume || 
					!libhal_drive_uses_removable_media(current->drive) ||
					libhal_drive_is_media_detected(current->drive));

			if(!current->friendly_name || strlen(current->friendly_name) == 0)
				continue;

			/* Look up the correct parent in the table */
			parent_treeiter = NULL;
			if(current->drive_udi)
				parent_treeiter = g_hash_table_lookup(udi_table, current->drive_udi);
			
			/*
			if(parent_treeiter && !gtk_tree_store_iter_is_valid(dialog->volume_model, parent_treeiter)) {
				g_warning("Iter wasn't valid! 0x%p", parent_treeiter);
				parent_treeiter = NULL;
			} */

			treeiter_list = g_slist_prepend(treeiter_list, (current_treeiter = g_new0(GtkTreeIter, 1)) );

			gtk_tree_store_insert_with_values(dialog->volume_model, current_treeiter, parent_treeiter, 0,
				DEV_COLUMN_UDI, current->udi, 
				DEV_COLUMN_NAME_MARKUP, current->friendly_name, 
				DEV_COLUMN_ICON, current->icon, 
				DEV_COLUMN_SENSITIVE, can_format, -1);

			g_hash_table_insert(udi_table, current->udi, current_treeiter);

			not_empty = TRUE;
		}

		device_list = NULL;
		if(!listed_drives && show_partitions) {
			listed_drives = TRUE;
			device_list = dialog->hal_volume_list;
		}
	}

	for(iter = treeiter_list; iter != NULL; iter = iter->next)
		g_free((GtkTreeIter*)iter->data);
	g_slist_free(treeiter_list);
	g_hash_table_destroy(udi_table);

	if(!not_empty) {
		gtk_tree_store_insert_with_values(dialog->volume_model, NULL, NULL, 0, 
				DEV_COLUMN_NAME_MARKUP, _("<i>No devices found</i>"), 
				DEV_COLUMN_SENSITIVE, FALSE, -1);
	}
}

static void
update_fs_sensitivity(FormatDialog* dialog, GtkTreeIter* level)
{
}

static void
update_extra_info(FormatDialog* dialog)
{
	gboolean show_info = FALSE;
	GtkLabel* info = dialog->extra_volume_info;

	/* Right now we only have one thing to display but it's possible that
	 * we might have more */

	/* Check to see if this has a mountpoint */
	do {
		char buf[512];
		GtkTreeIter iter;

		if(!gtk_combo_box_get_active_iter(dialog->volume_combo, &iter))
			break;

		const FormatVolume* vol = get_cached_device_from_treeiter(dialog, &iter);
		if(!vol || !vol->volume)
			break;

		const char* mountpoint = libhal_volume_get_mount_point(vol->volume);
		char* vol_name = get_friendly_volume_name(dialog->hal_context, vol->volume);
                
                if ( mountpoint == NULL ) {
                        const char *tmp;
                        tmp = libhal_volume_get_fstype(vol->volume);
                        
                        if ( tmp != NULL && strcmp (tmp, "swap") == 0 )
                                mountpoint = g_strdup(tmp);
                }
		
                /* FIXME: The \n is a hack to get the dialog box to not resize 
		 * horizontally so much */
		g_snprintf(buf, 512, _("<i>%s\n is currently mounted on/as '%s'</i>"), vol_name, mountpoint);
		g_free(vol_name);
		gtk_label_set_markup(info, buf);
		show_info |= TRUE;
	} while(0);

	if(show_info)
		gtk_widget_show_all(GTK_WIDGET(dialog->extra_volume_hbox));
	else
		gtk_widget_hide_all(GTK_WIDGET(dialog->extra_volume_hbox));
}

static void update_options_visibility(FormatDialog* dialog)
{
	GtkTreeIter iter = {0, NULL};
	gboolean valid_iter;

	valid_iter = gtk_combo_box_get_active_iter(dialog->volume_combo, &iter);
	const FormatVolume* dev = NULL;

	if(valid_iter)
		dev = get_cached_device_from_treeiter(dialog, &iter);

	if(!dev) {
		g_debug("Device is null!");
	}

	if(valid_iter && luks_valid_for_device(dev)) {
		gtk_widget_show_all(GTK_WIDGET(dialog->luks_subwindow));
	} else {
		gtk_widget_hide_all(GTK_WIDGET(dialog->luks_subwindow));
	}

	if(valid_iter && floppy_valid_for_device(dev)) {
		gtk_widget_show_all(GTK_WIDGET(dialog->floppy_subwindow));
	} else {
		gtk_widget_hide_all(GTK_WIDGET(dialog->floppy_subwindow));
	}
}

static void
update_sensitivity(FormatDialog* dialog)
{
	/* FIXME: We should probably disable other stuff while formatting too */
	gtk_widget_set_sensitive(GTK_WIDGET(dialog->format_button), !dialog->is_formatting);
}

static void
update_dialog(FormatDialog* dialog)
{
	rebuild_volume_combo(dialog);
	update_extra_info(dialog);
	update_options_visibility(dialog);
	update_sensitivity(dialog);
}


/*
 * Event handlers
 */

void
on_help_button_clicked (GtkWidget *w, gpointer user_data)
{
	GError *error = NULL;

	gnome_help_display_desktop_on_screen (NULL, "gformat", "gformat", "usage", 
					      gtk_widget_get_screen (w), &error);
	if (error) {
		show_error_dialog (gtk_widget_get_toplevel(w), _("Could not display help for the disk formatter."), error->message);
		g_error_free (error);
	}
}

void
on_volume_combo_changed(GtkWidget* w, gpointer user_data)
{
	FormatDialog* dialog = g_object_get_data( G_OBJECT(gtk_widget_get_toplevel(w)), "userdata" );
	update_extra_info(dialog);
	update_options_visibility(dialog);
}

void
on_close_button_clicked(GtkWidget* w, gpointer user_data)
{
	gtk_main_quit();
}

void
on_toplevel_delete_event(GtkWidget* w, gpointer user_data)
{
	gtk_main_quit();
}

void
on_show_partitions_toggled(GtkWidget* w, gpointer user_data) 
{
	FormatDialog* dialog = g_object_get_data( G_OBJECT(gtk_widget_get_toplevel(w)), "userdata" );
	update_dialog(dialog);
}
	
void on_libhal_device_added_removed(LibHalContext *ctx, const char *udi)
{
	g_debug("Added / Removed!");
	if( !libhal_device_query_capability(ctx, udi, "volume", NULL) &&
	    !libhal_device_query_capability(ctx, udi, "storage", NULL) )
		return;

	FormatDialog* dialog = libhal_ctx_get_user_data(ctx);
	update_dialog(dialog);
}

void on_libhal_prop_modified (LibHalContext *ctx,
			      const char *udi,
			      const char *key,
			      dbus_bool_t is_removed,
			      dbus_bool_t is_added)
{
	g_debug("Prop Modified!");
	if( !libhal_device_query_capability(ctx, udi, "volume", NULL) &&
	    !libhal_device_query_capability(ctx, udi, "storage", NULL) )
		return;

	FormatDialog* dialog = libhal_ctx_get_user_data(ctx);
	update_dialog(dialog);
}

void
on_format_button_clicked(GtkWidget* w, gpointer user_data)
{
	FormatDialog* dialog = g_object_get_data( G_OBJECT(gtk_widget_get_toplevel(w)), "userdata" );
	GError* err = NULL;

	g_debug("Hal version: %d", dialog->hal_version);

	/* XXX: The code here is completely cracked out, it's only here because
	 * I need to rewrite it */
	FormatVolume* vol;
	char* blockdev;
	char* fs;
	gboolean do_encrypt;
	gboolean do_partition_table;
	int partition_number;
	GHashTable* options;
	GError* error;

	/* Figure out the device params */
	GtkTreeIter iter;
	if(!gtk_combo_box_get_active_iter(dialog->volume_combo, &iter))
		return;
	if( !(vol = get_cached_device_from_treeiter(dialog, &iter)) )
		return;
	fs = get_fs_from_menu(dialog);

	if(!warn_user_of_impending_doom(dialog, vol)) {
		g_debug("User cancelled format!");
		return;
	}

	/* FIXME: Stop us from going into unfinished function */
	return;

	if(vol->volume) {
		blockdev = g_strdup(libhal_volume_get_device_file(vol->volume));
		partition_number = libhal_volume_get_partition_number(vol->volume);
		vol = vol;
	} else {
		/* TODO: Figure out what to do if any other partition is mounted on this drive */
		if(!(vol = write_partition_table(dialog, vol, fs)))
			goto error_out;
		
		blockdev = libhal_volume_get_device_file(vol->volume);
		partition_number = 1;
	}

#if 0
	if(!formatter_execute(dialog->formatter_list, 
				blockdev,
				fs,
				partition_number,
				options,
				&error)) {
		/* Always make sure there is an error on failure */
		error = (error ? error : g_error_new(0, -1, _("Unknown error")));
	}
#endif

	do_encrypt = FALSE;		/* FIXME: We have to figure this out */
	do_partition_table = TRUE;

error_out:
	g_free(blockdev);
	g_free(fs);

	return;
}


/*
 * Public functions
 */

FormatDialog*
format_dialog_new(void)
{
	FormatDialog *dialog;
	dialog = g_new0 (FormatDialog, 1);
	const char* xmlpath = GLADEDIR "/gformat.glade";
        
	dialog->xml = glade_xml_new (xmlpath, "toplevel", NULL);
	
	
        /* Try uninstalled next */
	if (!dialog->xml) {
		xmlpath = "./gformat.glade";
		dialog->xml = glade_xml_new (xmlpath, "toplevel", NULL);
	}

	if (dialog->xml == NULL){
		GtkWidget *f_dialog;
		f_dialog = gtk_message_dialog_new (NULL, 0,
				GTK_MESSAGE_ERROR,
				GTK_BUTTONS_OK,
				"Glade file for the format program is missing.\n"
				"Please check your installation of gnome-utils");
		gtk_dialog_run (GTK_DIALOG (f_dialog));
		gtk_widget_destroy (f_dialog);
		exit (1);
	} 

	/* Grab some widgets */
	dialog->toplevel = glade_xml_get_widget (dialog->xml, "toplevel");
	dialog->volume_combo = GTK_COMBO_BOX(glade_xml_get_widget(dialog->xml, "volume_combo"));
	dialog->show_partitions = GTK_TOGGLE_BUTTON(glade_xml_get_widget(dialog->xml, "show_partitions"));
	dialog->fs_combo = GTK_COMBO_BOX(glade_xml_get_widget(dialog->xml, "fs_combo"));
	dialog->extra_volume_info = GTK_LABEL(glade_xml_get_widget(dialog->xml, "extra_volume_info"));
	dialog->extra_volume_hbox = GTK_HBOX(glade_xml_get_widget(dialog->xml, "extra_volume_hbox"));
	dialog->progress_bar = GTK_PROGRESS_BAR(glade_xml_get_widget(dialog->xml, "progress_bar"));
	dialog->format_button = GTK_BUTTON(glade_xml_get_widget(dialog->xml, "format_button"));
	dialog->luks_subwindow = GTK_BOX(glade_xml_get_widget (dialog->xml, "luks_subwindow"));
	dialog->floppy_subwindow = GTK_BOX(glade_xml_get_widget (dialog->xml, "floppy_subwindow"));
	g_assert(dialog->toplevel != NULL);

	/* Get a HAL context; if we can't, bail */
	dialog->hal_context = libhal_context_alloc();

	glade_xml_signal_autoconnect(dialog->xml);
	g_object_set_data(G_OBJECT(dialog->toplevel), "userdata", dialog);

	/* Set stuff in the dialog up */
	setup_volume_treeview(dialog);	
	setup_filesystem_menu(dialog);
	dialog->fs_map = build_supported_fs_list();

	gtk_widget_show_all (dialog->toplevel);
	update_dialog(dialog);

	/* We do this here so they at least see the window before we die */
	if( !dialog->hal_context ) {
		show_error_dialog(dialog->toplevel, 
				_("Cannot get list of disks"), 
				_("Make sure the HAL daemon is running and configured correctly"));
		return NULL;
	}

	/* Get the HAL version and register the HAL device callbacks */
	get_hal_version(dialog);
	g_debug("Registering callback!");
	libhal_ctx_set_user_data(dialog->hal_context, dialog);
	if (libhal_ctx_set_device_added(dialog->hal_context, on_libhal_device_added_removed) == TRUE)
                printf("libhal_ctx_set_device_added called");
	libhal_ctx_set_device_removed(dialog->hal_context, on_libhal_device_added_removed);
	libhal_ctx_set_device_property_modified(dialog->hal_context, on_libhal_prop_modified);
	libhal_ctx_set_device_new_capability(dialog->hal_context, NULL);
	libhal_ctx_set_device_lost_capability(dialog->hal_context, NULL);
	libhal_ctx_set_device_condition(dialog->hal_context, NULL);

	return dialog;
}


void format_dialog_free(FormatDialog* obj)
{
	if(obj->hal_context)
		libhal_ctx_free(obj->hal_context);
	
	/* We have destroy notify hooks, so we don't worry about what's inside */
	if(obj->icon_cache)
		g_hash_table_destroy(obj->icon_cache);

	if(obj->hal_drive_list)
		format_volume_list_free(obj->hal_drive_list);

	if(obj->hal_volume_list)
		format_volume_list_free(obj->hal_volume_list);

	if(obj->hal_context)
		libhal_ctx_free(obj->hal_context);

	/* Free our windows */
	g_object_unref(obj->luks_subwindow);
	g_object_unref(obj->floppy_subwindow);
	g_object_unref(obj->toplevel);
	g_object_unref(obj->xml);

	g_free(obj);
}
