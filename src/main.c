/*
 * main.c
 *
 * Copyright 2007 Riccardo Setti <giskard@autistici.org>
 * 		  Paul Betts <paul.betts@gmail.com>
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


#include "config.h"
#include <stdio.h>
#include <errno.h>

#include <libhal.h>
#include <libhal-storage.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glade/glade.h>
#include <gtk/gtk.h>

#include "format-dialog.h"

/* Command-line stuff */
static GOptionEntry entries[] = 
{
	{ NULL }
};


int
main (int argc, char *argv[])
{
	/* Initialize gettext support */
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);

	/* Parse the command line */
	GError *error = NULL;
	GOptionContext* context = g_option_context_new ( _("- Formats a removable disk") );
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_add_group (context, gtk_get_option_group(TRUE));
	g_option_context_parse (context, &argc, &argv, &error);
	g_option_context_free (context);

	gtk_init(&argc, &argv);

	FormatDialog *dialog;
	dialog = format_dialog_new();
	if(dialog != NULL) {
		gtk_main ();
		format_dialog_free(dialog);
	}
  
  PedDevice = dev;
  dev = ped_device_get("/dev/sda1")
  g_free(dev);
	
  return 0;

};
