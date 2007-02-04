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

#include "format-dialog.h"
#include "device-info.h"

enum {
	COLUMN_UDI = 0,
	COLUMN_ICON,
	COLUMN_NAME_MARKUP,
	COLUMN_SENSITIVE,
};


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
	gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(combo), icon_renderer, "pixbuf", COLUMN_ICON );
	gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(combo), icon_renderer, "sensitive", COLUMN_SENSITIVE );
	gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(combo), text_renderer, "markup", COLUMN_NAME_MARKUP );
	gtk_cell_layout_add_attribute( GTK_CELL_LAYOUT(combo), text_renderer, "sensitive", COLUMN_SENSITIVE );

	/* Do some miscellaneous things */
	dialog->icon_cache = create_icon_cache();
	dialog->volume_model = model;
}

static void
rebuild_volume_combo(FormatDialog* dialog)
{
	/* FIXME: This code got far too out of hand and as a result is very
	 * hacky. However, it works, so there's that */

	g_assert(dialog && dialog->volume_model);

	gboolean show_partitions = gtk_toggle_button_get_active(dialog->show_partitions);
	GSList *drive_list = build_volume_list(dialog->hal_context, FORMATVOLUMETYPE_DRIVE,
			dialog->icon_cache, 22, 22);

	if(!drive_list) {
		show_error_dialog(dialog->toplevel, 
				_("Cannot get list of disks"), 
				_("Make sure the HAL daemon is running and configured correctly"));
		return;
	}

	/* Iterate through the volume list and build the tree model. If we're
	 * listing partitions, it's a bit trickier: first we add all the drives,
	 * and make a udi=>GtkTreeIter table. Then we use that table to add
	 * the partitions to the correct drives */
	GSList* treeiter_list = NULL, *device_list = drive_list;
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
			g_debug("Parent treeiter: 0x%p", parent_treeiter);
			if(parent_treeiter && !gtk_tree_store_iter_is_valid(dialog->volume_model, parent_treeiter)) {
				g_warning("Iter wasn't valid! 0x%p", parent_treeiter);
				parent_treeiter = NULL;
			}

			treeiter_list = g_slist_prepend(treeiter_list, (current_treeiter = g_new0(GtkTreeIter, 1)) );

			gtk_tree_store_insert_with_values(dialog->volume_model, current_treeiter, parent_treeiter, 0,
				COLUMN_UDI, current->udi, 
				COLUMN_NAME_MARKUP, current->friendly_name, 
				COLUMN_ICON, current->icon, 
				COLUMN_SENSITIVE, can_format, -1);

			g_hash_table_insert(udi_table, current->udi, current_treeiter);

			not_empty = TRUE;
		}

		if(!listed_drives && show_partitions) {
			listed_drives = TRUE;
			device_list = build_volume_list(dialog->hal_context, FORMATVOLUMETYPE_VOLUME,
					dialog->icon_cache, 22, 22);
		}
		else {
			format_volume_list_free(device_list);
			if(device_list == drive_list)	drive_list = NULL;
			device_list = NULL;
		}
	}

	for(iter = treeiter_list; iter != NULL; iter = iter->next)
		g_free((GtkTreeIter*)iter->data);
	g_slist_free(treeiter_list);
	g_hash_table_destroy(udi_table);

	if(drive_list)
		format_volume_list_free(drive_list);

	if(!not_empty) {
		gtk_tree_store_insert_with_values(dialog->volume_model, NULL, NULL, 0, 
				COLUMN_NAME_MARKUP, _("<i>No devices found</i>"), 
				COLUMN_SENSITIVE, FALSE, -1);
	}
}

gchar*
get_udi_from_iter(FormatDialog* dialog, GtkTreeIter* iter)
{
	char* ret;
	GValue val = {0, };
	gtk_tree_model_get_value(GTK_TREE_MODEL(dialog->volume_model), iter, COLUMN_UDI, &val);
	ret = g_strdup(g_value_get_string(&val));
	g_value_unset(&val);
	return ret;
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

		char* udi = get_udi_from_iter(dialog, &iter);
		if(!udi) {
			g_error("UDI of device was blank!");
			break;
		}
		LibHalVolume* vol = libhal_volume_from_udi(dialog->hal_context, udi);
		if(!vol) {
			g_free(udi);
			break;
		}

		const char* mountpoint = libhal_volume_get_mount_point(vol);
		if(!mountpoint) {
			g_free(udi);
			libhal_volume_free(vol);
			break;
		}
		char* vol_name = get_friendly_volume_name(dialog->hal_context, vol);

		g_snprintf(buf, 512, _("<i>%s is currently mounted on %s</i>"), vol_name, mountpoint);
		g_free(vol_name);
		gtk_label_set_markup(dialog->extra_volume_info, buf);
		show_info |= TRUE;
	} while(0);

	if(show_info)
		gtk_widget_show_all(GTK_WIDGET(dialog->extra_volume_hbox));
	else
		gtk_widget_hide_all(GTK_WIDGET(dialog->extra_volume_hbox));
}

static void
update_dialog(FormatDialog* dialog)
{
	rebuild_volume_combo(dialog);
	update_extra_info(dialog);
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
	

/*
 * Public functions
 */

FormatDialog*
format_dialog_new(void)
{
	FormatDialog *dialog;
	dialog = g_new0 (FormatDialog, 1);

	dialog->xml = glade_xml_new (GLADEDIR "/gformat.glade", NULL, NULL);
	
	/* Try uninstalled next */
	if (!dialog->xml) {
		dialog->xml = glade_xml_new ("./gformat.glade", NULL, NULL);
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

	/* Get a HAL context; if we can't, bail */
	dialog->hal_context = libhal_context_alloc();

	glade_xml_signal_autoconnect(dialog->xml);
	g_object_set_data(G_OBJECT(dialog->toplevel), "userdata", dialog);
	setup_volume_treeview(dialog);	

	gtk_widget_show_all (dialog->toplevel);
	update_dialog(dialog);

	/* We do this here so they at least see the window before we die */
	if( !dialog->hal_context ) {
		show_error_dialog(dialog->toplevel, 
				_("Cannot get list of disks"), 
				_("Make sure the HAL daemon is running and configured correctly"));
		return NULL;
	}

	return dialog;
}


void format_dialog_free(FormatDialog* obj)
{
	if(obj->hal_context)
		libhal_ctx_free(obj->hal_context);
	
	/* We have destroy notify hooks, so we don't worry about what's inside */
	if(obj->icon_cache)
		g_hash_table_destroy(obj->icon_cache);

	g_free(obj);
}
