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
#include "format-dialog.h"
#include "formatterbase.h"
#include "fs-parted.h"

enum {
	DEV_COLUMN_UDI = 0,
	DEV_COLUMN_ICON,
	DEV_COLUMN_NAME_MARKUP,
	DEV_COLUMN_SENSITIVE,
};

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
luks_valid_for_device (const FormatVolume* dev)
{
	/* AFAIK, you can format LUKS on anything that is above a certain size */
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

static void
setup_filesystem_menu(FormatDialog* dialog)
{
	/* Track down the menu we want to add subitems to */
	GtkMenuItem* fs_menu_item =  GTK_MENU_ITEM(glade_xml_get_widget(dialog->xml, "menu_fs_list"));
	GtkMenuShell* fs_menu = GTK_MENU_SHELL(gtk_menu_new());

	GSList* iter;
	GHashTable* no_dups_list = g_hash_table_new(g_str_hash, g_str_equal);
	for(iter = dialog->formatter_list; iter != NULL; iter = g_slist_next(iter)) {
		Formatter* current = iter->data;
	
		/* We do the hash nonsense to make sure we don't add duplicates
		 * (ie parted and mkfs.ext2 both know how to format ext2 or something) */
		int i;
		for(i = 0; current->available_fs_list[i] != NULL; i++) {
			const char* current_fs = current->available_fs_list[i];
			if( g_hash_table_lookup(no_dups_list, current_fs) )
				continue;

			GtkWidget* new_menu = gtk_menu_item_new_with_label(current_fs);
			gtk_widget_set_name(new_menu, current_fs);
			gtk_menu_shell_append(fs_menu, new_menu);
			g_hash_table_insert(no_dups_list, current_fs, current);
		}
	}

	g_hash_table_destroy(no_dups_list);
	gtk_menu_item_set_submenu(fs_menu_item, GTK_WIDGET(fs_menu));
}

static void
setup_formatter_backends(FormatDialog* dialog)
{
	/* Load the various backends that can format block devices */
	Formatter* parted = parted_formatter_init();
	if(parted)
		dialog->formatter_list = g_slist_prepend(dialog->formatter_list, parted);
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
update_dialog(FormatDialog* dialog)
{
	rebuild_volume_combo(dialog);
	update_extra_info(dialog);
	update_options_visibility(dialog);
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
		show_error_dialog (gtk_widget_get_toplevel(w), _("Could not display help for the floppy formatter."), error->message);
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

	dialog->toplevel = glade_xml_get_widget (dialog->xml, "toplevel");
	dialog->volume_combo = GTK_COMBO_BOX(glade_xml_get_widget(dialog->xml, "volume_combo"));
	dialog->show_partitions = GTK_TOGGLE_BUTTON(glade_xml_get_widget(dialog->xml, "show_partitions"));
	dialog->extra_volume_info = GTK_LABEL(glade_xml_get_widget(dialog->xml, "extra_volume_info"));
	dialog->extra_volume_hbox = GTK_HBOX(glade_xml_get_widget(dialog->xml, "extra_volume_hbox"));
	g_assert(dialog->toplevel != NULL);

	dialog->luks_subwindow = GTK_BOX(glade_xml_get_widget (dialog->xml, "luks_subwindow"));
	dialog->floppy_subwindow = GTK_BOX(glade_xml_get_widget (dialog->xml, "floppy_subwindow"));

	/* Get a HAL context; if we can't, bail */
	dialog->hal_context = libhal_context_alloc();

	glade_xml_signal_autoconnect(dialog->xml);
	g_object_set_data(G_OBJECT(dialog->toplevel), "userdata", dialog);

	/* Set stuff in the dialog up */
	setup_volume_treeview(dialog);	
	setup_formatter_backends(dialog);
	setup_filesystem_menu(dialog);

	gtk_widget_show_all (dialog->toplevel);
	update_dialog(dialog);

	/* We do this here so they at least see the window before we die */
	if( !dialog->hal_context ) {
		show_error_dialog(dialog->toplevel, 
				_("Cannot get list of disks"), 
				_("Make sure the HAL daemon is running and configured correctly"));
		return NULL;
	}

	/* Register the HAL device callbacks */
	g_debug("Registering callback!");
	libhal_ctx_set_user_data(dialog->hal_context, dialog);
	libhal_ctx_set_device_added(dialog->hal_context, on_libhal_device_added_removed);
	libhal_ctx_set_device_removed(dialog->hal_context, on_libhal_device_added_removed);
	libhal_ctx_set_device_property_modified(dialog->hal_context, on_libhal_prop_modified);

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
