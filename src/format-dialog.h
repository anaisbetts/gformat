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

	/* HAL info */
	LibHalContext* hal_context;
	GSList* hal_drive_list;		/* List of FormatVolume ptrs */
	GSList* hal_volume_list; 	/* this too */
} FormatDialog;

FormatDialog* format_dialog_new(void);
void format_dialog_free(FormatDialog* obj);

#endif
