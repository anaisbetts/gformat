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
rebuild_volume_combo(FormatDialog* dlg)
{
	g_assert(dlg && dlg->volume_model);

	gboolean show_partitions = gtk_toggle_button_get_active(dlg->show_partitions);
	GSList* volume_list = build_volume_list(dlg->hal_context, 
			(show_partitions ? FORMATVOLUMETYPE_VOLUME : FORMATVOLUMETYPE_DRIVE),
			dlg->icon_cache, 22, 22);

	if(!volume_list) {
		show_error_dialog(dlg->toplevel, 
				_("Cannot get list of disks"), 
				_("Make sure the HAL daemon is running and configured correctly"));
		return;
	}

	/* Iterate through the volume list and build the tree model */
	GSList* iter;	gboolean not_empty = FALSE, can_format;
	FormatVolume* current;

	gtk_tree_store_clear(dlg->volume_model);
	for(iter = volume_list; iter != NULL; iter=iter->next) {
		current = iter->data;
		can_format = (current->volume || 
				!libhal_drive_uses_removable_media(current->drive) ||
				libhal_drive_is_media_detected(current->drive));

		if(!current->friendly_name || strlen(current->friendly_name) == 0)
			continue;

		/* FIXME: It'd be nice if the partitions were a subset of the
		 * drive that owned them */
		gtk_tree_store_insert_with_values(dlg->volume_model, NULL, NULL, 0,
			COLUMN_NAME_MARKUP, current->friendly_name, 
			COLUMN_ICON, current->icon, 
			COLUMN_SENSITIVE, can_format, -1);

		not_empty = TRUE;
	}

	if(!not_empty) {
		gtk_tree_store_insert_with_values(dlg->volume_model, NULL, NULL, 0, 
				COLUMN_NAME_MARKUP, _("<i>No devices found</i>"), 
				COLUMN_SENSITIVE, FALSE, -1);
	}

	if(volume_list)
		format_volume_list_free(volume_list);
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
	FormatDialog* dlg = g_object_get_data( G_OBJECT(gtk_widget_get_toplevel(w)), "userdata" );
	rebuild_volume_combo(dlg);
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
	g_assert(dialog->toplevel != NULL);

	/* Get a HAL context; if we can't, bail */
	dialog->hal_context = libhal_context_alloc();

	glade_xml_signal_autoconnect(dialog->xml);
	g_object_set_data(G_OBJECT(dialog->toplevel), "userdata", dialog);
	setup_volume_treeview(dialog);	
	rebuild_volume_combo(dialog);

	gtk_widget_show_all (dialog->toplevel);

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
