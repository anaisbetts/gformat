/*
 * gnome-format-dialog.h - discover block device
 *
 * Copyright 2007 Riccardo Setti <giskard@autistici.org>
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

#ifndef _FORMAT_DIALOG_H
#define _FORMAT_DIALOG_H

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <libgnomeui/gnome-help.h>
#include <libgnomeui/gnome-ui-init.h>
#include <glade/glade.h>

#include <libhal.h>
#include <libhal-storage.h>

#include "device-info.h"

typedef struct _FormatDialog {
	GladeXML* xml;
	GtkWidget* toplevel;

	/* Subwindows */
	GtkBox* luks_subwindow;
	GtkBox* floppy_subwindow;

	/* Stuff for device list */
	GtkTreeStore* volume_model;
	GtkComboBox* volume_combo;
	GtkToggleButton* show_partitions;
	GHashTable* icon_cache;
	GtkLabel* extra_volume_info;
	GtkHBox* extra_volume_hbox;
	GtkButton* format_button;
	GtkButton* cancel_button;
	GtkProgressBar* progress_bar;

	/* Stuff for filesystem list */
	GtkComboBox* fs_combo;
	GtkTreeStore* fs_model;
	GHashTable* fs_map; 		/* This maps fs names ("ext3") to its script */

	/* HAL info */
        gint hal_version; 		/* "1.3.4.5" => 1345 */
	LibHalContext* hal_context;
	GSList* hal_drive_list;		/* List of FormatVolume ptrs */
	GSList* hal_volume_list; 	/* this too */

	/* Progress bar stuff */
	gint total_ops;
	gint ops_left; 			/* (ops_left == 0) => not formatting */
	GError* format_error;
	char* mkfs_target;
} FormatDialog;

FormatDialog* format_dialog_new(void);
void format_dialog_free(FormatDialog* obj);

/* Progress bar functions */
void handle_format_error(FormatDialog* dialog);
void start_operation(FormatDialog* dialog, int steps);
void finish_operation(FormatDialog* dialog);
void do_next_operation(FormatDialog* dialog, const gchar* progress_text);

/* Functions to help out format tasks */
gchar* get_fs_from_menu(FormatDialog* dialog);

#endif
