/* main.c
 *
 * Copyright (C) 1999 Red Hat, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gconf/gconf-client.h>
#include <libgnomeui/gnome-help.h>
#include <libgnomeui/gnome-ui-init.h>
#include <glade/glade.h>

#ifdef USE_HAL
# include <libhal.h>
#endif

#include "gfloppy.h"
#include "gfloppy-config.h"
#include "progress.h"

#define NUM_DEVICES_TO_CHECK 4

static GFloppy floppy;
static GladeXML *xml;
static GtkWidget *toplevel = NULL;

gboolean devfs_mode = FALSE;
gboolean prefer_mkdosfs_backend;

/* Only needed if more then one device exists */
static GList *valid_devices = NULL;


typedef struct _FloppyStatus
{
	GFloppyStatus status;
	gchar *device_path;
} FloppyStatus;


static GFloppyFormattingMode get_selected_formatting_mode ();
static void type_option_changed_cb     (GtkOptionMenu   *option_menu,
					gpointer         data);
/*
static void set_widget_sensitive_from  (GtkToggleButton *button,
					gpointer         data);
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
floppy_status_free (FloppyStatus *floppies, gint num_floppies)
{
	gint i;

	for (i = 0; i < num_floppies; i++) {
		g_free (floppies[i].device_path);
	}

	g_free (floppies);
}

static char *
get_mformat_device (char *device)
{
	/* FIXME: what the heck is /dev/fd2 and /dev/fd3 ?? */
	if (!strcmp (device, "/dev/floppy/1") || !strcmp (device, "/dev/fd1"))
		return "b:";

	return "a:";
}

static void
devices_option_activated (GtkWidget *menu_item, char *device)
{
	g_free (floppy.device);
	g_free (floppy.mdevice);

	floppy.device = g_strdup (device);
	floppy.mdevice = g_strdup (get_mformat_device (device));
}

static void
start_format (void)
{
	pipe (floppy.message);

	/* setup size field based on user request */
	floppy.nblocks = floppy_block_size (floppy.size);
	floppy.nbadblocks = 0;
	floppy.pid = fork ();

	if (floppy.pid < 0) {
		g_error ("unable to fork ().\nPlease free up some resources and try again.\n");
		_exit (1);
	}

	if (floppy.pid == 0) {
		/* child */
		close (floppy.message [0]);
		close (STDERR_FILENO);
		close (STDOUT_FILENO);
		format_floppy (&floppy);
		_exit (0);
	}
	close (floppy.message [1]);

	fcntl (floppy.message [0], F_SETFL, O_NONBLOCK);
	setup_progress_and_run (&floppy, toplevel);
}

#ifdef USE_HAL

#define HAL_FLOPPY_KEY   "storage.drive_type"
#define HAL_FLOPPY_VALUE "floppy"

#define HAL_FLOPPY_DEV_KEY "block.device"

static LibHalContext *
get_hal_context ()
{
	LibHalContext *hal_ctx;
	hal_ctx = NULL;

	hal_ctx = libhal_ctx_new ();
	if (hal_ctx) {
		DBusConnection *dbus_conn;
		DBusError error;

		dbus_error_init (&error);
		dbus_conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);

		if (dbus_error_is_set (&error)) {
			dbus_error_free (&error);
			return NULL;
		}

		libhal_ctx_set_dbus_connection (hal_ctx, dbus_conn);

		if (!libhal_ctx_init (hal_ctx, &error)) {
			dbus_error_free (&error);
			libhal_ctx_free (hal_ctx);
 			return NULL;
 		}
	}

	return hal_ctx;
}

static FloppyStatus *
get_hal_floppies (gint *num_devices, gint *num_ok, LibHalContext *ctx)
{
	gint              i;
	gchar           **device_list;
	FloppyStatus *floppies;

	device_list = libhal_manager_find_device_string_match (ctx,
							       HAL_FLOPPY_KEY, 
							       HAL_FLOPPY_VALUE, 
							       num_devices, 
							       NULL); 

	floppies = NULL;
	
	*num_ok = 0;

	if (*num_devices != 0) {
		floppies = g_new0 (FloppyStatus, *num_devices);

		for (i = 0; i < *num_devices; i++) {
			floppies[i].device_path = 
				libhal_device_get_property_string (ctx, 
								device_list[i], 
								HAL_FLOPPY_DEV_KEY,
								NULL);

			floppies[i].status = test_floppy_device (floppies[i].device_path);

			if (floppies[i].status == GFLOPPY_DEVICE_OK)
				(*num_ok)++;
		}
		
		libhal_free_string_array (device_list);
	} else {
		*num_devices = 1;
		
		floppies = g_new0 (FloppyStatus, 1);
		
		floppies[0].status = GFLOPPY_NO_DEVICE;
	}

	return floppies;
}

#endif

static void
init_commands (void)
{
	const char *path;
	char *newpath;

	path = g_getenv ("PATH");

	if (path)
		newpath = g_strconcat ("PATH=", path, ":/sbin:/usr/sbin:/usr:/usr/bin", NULL);
	else
		newpath = g_strdup ("PATH=/sbin:/usr/sbin:/usr:/usr/bin");

	putenv (newpath); /* Sigh, we have to leak it */

	floppy.mke2fs_cmd    = g_find_program_in_path ("mke2fs");
	floppy.mkdosfs_cmd   = g_find_program_in_path ("mkdosfs");
	floppy.mformat_cmd   = g_find_program_in_path ("mformat");
	floppy.badblocks_cmd = g_find_program_in_path ("mbadblocks");

	if (!floppy.mke2fs_cmd &&
	    !floppy.mkdosfs_cmd &&
	    !floppy.mformat_cmd) {
		show_error_dialog (NULL, _("Cannot Format"),
				   _("Neither the mke2fs nor the mkdosfs/mformat applications are installed. You can't format a floppy without one of them."));
		exit (1);
	}
}

static void
init_devices (gpointer ctx)
{
	GtkWidget *device_option;
	GtkWidget *device_entry;
	GtkWidget *device_label;
	char *msg = NULL;
	gint ok_devices_present = 0;

	/* First check --device arg */
	if (floppy.device != NULL) {
		GFloppyStatus status;
		status = test_floppy_device (floppy.device);
		switch (status) {
		case GFLOPPY_NO_DEVICE:
			msg = g_strdup_printf (_("Unable to open the device %s, formatting cannot continue."), floppy.device);
			break;
		case GFLOPPY_DEVICE_DISCONNECTED:
			msg = g_strdup_printf (_("The device %s is disconnected.\nPlease attach device to continue."), floppy.device);
			break;
		case GFLOPPY_INVALID_PERMISSIONS:
			msg = g_strdup_printf (_("You do not have the proper permissions to write to %s, formatting will not be possible.\nContact your system administrator about getting write permissions."), floppy.device);
			break;
		default:
			break;
		}
		if (msg) {
			GtkWidget *toplevel = glade_xml_get_widget (xml, "toplevel");
			show_error_dialog (toplevel, _("Cannot initialize device"), msg);
			exit (1);
		}
		/* Device is okay */
		ok_devices_present = 1;
	} else {
		gint num_devices_to_check = NUM_DEVICES_TO_CHECK;

		gint i;
		FloppyStatus *floppies;

#ifdef USE_HAL
		gint num_hal_floppies_ok;

		if (ctx != NULL) {
			floppies = get_hal_floppies (&num_devices_to_check, 
						     &num_hal_floppies_ok, 
						     (LibHalContext *) ctx);

			ok_devices_present += num_hal_floppies_ok;
		} 
		else 
		/* If hal didn't initialize properly, fall back to the manual way */
#endif 
		{
			floppies = g_new0 (FloppyStatus, num_devices_to_check);
			
			for (i = 0; i < num_devices_to_check; i++) {
				floppies[i].device_path = g_new0 (gchar, 32);
				sprintf (floppies[i].device_path,"/dev/floppy/%d", i);
				floppies[i].status = test_floppy_device (floppies[i].device_path);
				
				if (floppies[i].status == GFLOPPY_NO_DEVICE && !devfs_mode) {
					sprintf (floppies[i].device_path,"/dev/fd%d", i);
					floppies[i].status = test_floppy_device (floppies[i].device_path);
				} else
					devfs_mode = TRUE;
				
				if (floppies[i].status == GFLOPPY_DEVICE_OK) 
					ok_devices_present++;
			}
		}

		if (ok_devices_present == 0) {
			GtkWidget *toplevel;

			/* FIXME: Let's assume that there's only a problem with /dev/fd0 */
			switch (floppies[0].status) {
			case GFLOPPY_NO_DEVICE:
				msg = g_strdup_printf (_("Unable to open any device, formatting cannot continue."));
				break;
			case GFLOPPY_DEVICE_DISCONNECTED:
				msg = g_strdup_printf (_("The device %s is disconnected.\nPlease attach device to continue."), _("/dev/floppy/0 or /dev/fd0"));
				break;
			case GFLOPPY_INVALID_PERMISSIONS:
				msg = g_strdup_printf (_("You do not have the proper permissions to write to %s, formatting will not be possible.\nContact your system administrator about getting write permissions."), _("/dev/floppy/0 or /dev/fd0"));
				break;
			default:
				g_assert_not_reached ();
			}

			toplevel = glade_xml_get_widget (xml, "toplevel");
			gtk_widget_set_sensitive (toplevel, FALSE);
			show_error_dialog (toplevel, _("Cannot initialize device"), msg);
			exit (1);
		} else if (ok_devices_present == 1) {
			for (i = 0; i < num_devices_to_check; i++)
				if (floppies[i].status == GFLOPPY_DEVICE_OK) {
					floppy.device = g_strdup (floppies[i].device_path);
					break;
				}
		} else {
			for (i = 0; i < num_devices_to_check; i++)
				if (floppies[i].status == GFLOPPY_DEVICE_OK) {
					valid_devices = g_list_append (valid_devices, 
								       g_strdup (floppies[i].device_path));

					if (floppy.device == NULL)
						floppy.device = g_strdup (floppies[i].device_path);
				}
		}

		floppy_status_free (floppies, num_devices_to_check);
	}

	floppy.mdevice = g_strdup (get_mformat_device (floppy.device));

	/* set up the UI */
	device_option = glade_xml_get_widget (xml, "device_option");
	device_entry = glade_xml_get_widget (xml, "device_entry");
	device_label = glade_xml_get_widget (xml, "device_label");

	if (ok_devices_present == 1) {
		gtk_widget_hide (device_option);
		gtk_widget_show (device_entry);
		gtk_entry_set_text (GTK_ENTRY (device_entry), floppy.device);
		gtk_label_set_mnemonic_widget (GTK_LABEL(device_label), GTK_WIDGET(device_entry));	
	} else {
		GtkWidget *menu;
		GList *list;

		gtk_widget_show (device_option);
		gtk_widget_hide (device_entry);
		gtk_label_set_mnemonic_widget (GTK_LABEL(device_label), GTK_WIDGET(device_option));		
		gtk_option_menu_remove_menu (GTK_OPTION_MENU (device_option));
		menu = gtk_menu_new ();
		for (list = valid_devices; list; list = list->next) {
			GtkWidget *menu_item;
			menu_item = gtk_menu_item_new_with_label ((char *)list->data);
			gtk_signal_connect (GTK_OBJECT (menu_item), "activate", GTK_SIGNAL_FUNC (devices_option_activated), list->data);
			gtk_widget_show (menu_item);
			gtk_menu_append (GTK_MENU (menu), menu_item);
		}
		gtk_widget_show_all (menu);
		gtk_option_menu_set_menu (GTK_OPTION_MENU (device_option), menu);
		gtk_option_menu_set_history (GTK_OPTION_MENU (device_option), 0);
	}
}

static GFloppyFormattingMode
get_selected_formatting_mode ()
{
	GtkWidget *quick_format_rb;
	GSList *l;
	gint i;

        quick_format_rb = glade_xml_get_widget (xml, "quick_format_rb");

	l = gtk_radio_button_get_group (GTK_RADIO_BUTTON (quick_format_rb));

	/* Note: don't doubt of the decrementation, that's because of the
		 group's list whose values are inserted at its head.
		 Perhaps it may be safier to just play the "if-else" war. */

	for (i = 2; i < GFLOPPY_FORMAT_END; i--, l = l->next)
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (l->data)))
			return i;

	g_assert_not_reached ();
	return i;
}

static void
type_option_changed_cb (GtkOptionMenu *option_menu,
			gpointer       data)
{
	GtkOptionMenu *type_om;
	GtkWidget *note_label;
	GtkWidget *standard_format_rb;
	GtkWidget *thorough_format_rb;
	gboolean cannot_check;

	type_om = GTK_OPTION_MENU (option_menu);
	standard_format_rb = glade_xml_get_widget (xml, "standard_format_rb");
	thorough_format_rb = glade_xml_get_widget (xml, "thorough_format_rb");
	note_label = glade_xml_get_widget (xml, "note_label");

	cannot_check = gtk_option_menu_get_history (type_om) == 1 && /* fat fs */
		       !floppy.mkdosfs_backend &&		     /* mtools backend */
		       floppy.badblocks_cmd == NULL;		     /* mbadblocks not installed */

	if (cannot_check &&
	    gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (thorough_format_rb)))
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (standard_format_rb), TRUE);

	gtk_widget_set_sensitive (thorough_format_rb, !cannot_check);

	if (cannot_check)
		gtk_widget_show (note_label);
	else
		gtk_widget_hide (note_label);
}

/* 
static void
set_widget_sensitive_from (GtkToggleButton *button,
			   gpointer         data)
{
	GtkWidget *widget;
	gboolean active;

	g_return_if_fail (data != NULL);
	g_return_if_fail (GTK_IS_WIDGET (data));

	widget = GTK_WIDGET (data);
	active = gtk_toggle_button_get_active (button);

	gtk_widget_set_sensitive (widget, active);

	if (active)
		gtk_widget_grab_focus (widget);
}
*/

gint
on_toplevel_delete_event (GtkWidget *w, GdkEventKey *e)
{
	gtk_main_quit ();

	return TRUE;
}

void
on_close_button_clicked (GtkWidget *widget, gpointer user_data)
{
	gtk_main_quit ();
}

void
on_help_button_clicked (GtkWidget *widget, gpointer user_data)
{
	GError *error = NULL;

	gnome_help_display_desktop_on_screen (NULL, "gfloppy", "gfloppy", "usage", 
					      gtk_widget_get_screen (widget), &error);
	if (error) {
		show_error_dialog (toplevel, _("Could not display help for the floppy formatter."), error->message);
		g_error_free (error);
	}
}

void
on_format_button_clicked (GtkWidget *widget, gpointer user_data)
{
	GtkWidget *type_option;
	GtkWidget *density_option;
	GtkWidget *volume_name_entry;

        type_option = glade_xml_get_widget (xml, "type_option");
        density_option = glade_xml_get_widget (xml, "density_option");
	volume_name_entry = glade_xml_get_widget (xml, "volume_name_entry");

	floppy.volume_name = (char *) gtk_entry_get_text (GTK_ENTRY (volume_name_entry));

	if (strstr (floppy.volume_name, " ")) {
		show_error_dialog (toplevel, _("Incorrect volume name"),
				   _("The volume name can't contain any blank space."));
		return;
	}

	if (strlen (floppy.volume_name) == 0)
		floppy.volume_name = NULL;

	gtk_widget_set_sensitive (toplevel, FALSE);

	/* If we can format in either types */
	if (floppy.mke2fs_cmd && (floppy.mkdosfs_cmd || floppy.mformat_cmd))
		floppy.type = gtk_option_menu_get_history (GTK_OPTION_MENU (type_option)); /* user choice */
	else
		floppy.type = floppy.mke2fs_cmd ? GFLOPPY_E2FS : GFLOPPY_FAT; /* otherwise just use the available one */

	floppy.size = gtk_option_menu_get_history (GTK_OPTION_MENU (density_option));
	floppy.formatting_mode = get_selected_formatting_mode ();

        start_format ();

        gtk_widget_set_sensitive (toplevel, TRUE);
}

int
main (int argc, char *argv[])
{
	GnomeProgram *program;
	GOptionContext *context;
	GConfClient *client;
	GFloppyConfig config;
	GtkWidget *fs_entry;
	GtkWidget *type_option;
	GtkWidget *quick_format_rb;
	GtkWidget *default_mode_rb;
	GtkWidget *label;
	GtkSizeGroup *size_group;
	GSList *l;
	gchar *label_text;

	/* This is for the libhal context, should we need it */
	gpointer ctx;

	static const GOptionEntry options[] =  {
	  { "device", 0, 0, G_OPTION_ARG_STRING, &(floppy.device),
	    N_("The device to format"), N_("DEVICE") },
	  { NULL }
	};

	floppy.device = NULL;

	bindtextdomain(GETTEXT_PACKAGE, GNOMELOCALEDIR);
	bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
	textdomain(GETTEXT_PACKAGE);

	context = g_option_context_new (_("- Floppy Formatter"));
	
	g_option_context_add_main_entries (context, options, GETTEXT_PACKAGE);

	program = gnome_program_init
	  (argv[0], VERSION, LIBGNOMEUI_MODULE,
	   argc, argv,
	   GNOME_PARAM_GOPTION_CONTEXT, context,
	   GNOME_PARAM_APP_DATADIR,DATADIR,
	   GNOME_PARAM_NONE);

#ifdef USE_HAL
	ctx = (gpointer)get_hal_context ();
#endif

	gtk_window_set_default_icon_name ("gnome-dev-floppy");

	init_commands ();

	/* Now we can set up glade */
	glade_gnome_init();

        xml = glade_xml_new (GLADEDIR "/gfloppy2.glade", NULL, NULL);
	if (xml == NULL)
		g_error ("Cannot load/find gfloppy2.glade");

	/* We get the different widgets */
	
	toplevel = glade_xml_get_widget (xml, "toplevel");
	type_option = glade_xml_get_widget (xml, "type_option");
        quick_format_rb = glade_xml_get_widget (xml, "quick_format_rb");

	size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	
	label = glade_xml_get_widget (xml, "device_label");
	gtk_size_group_add_widget (size_group, label);

	label = glade_xml_get_widget (xml, "density_label");
	gtk_size_group_add_widget (size_group, label);
	
	label = glade_xml_get_widget (xml, "type_label");
	gtk_size_group_add_widget (size_group, label);
	
	label = glade_xml_get_widget (xml, "volume_name_label");
	gtk_size_group_add_widget (size_group, label);
 
	/* Signals connection */
	
	glade_xml_signal_autoconnect (xml);

	g_signal_connect (type_option, "changed",
			  G_CALLBACK (type_option_changed_cb), NULL);

	/* GConf support */
	client = gconf_client_get_default ();
	gconf_client_add_dir (client, "/apps/gfloppy",
			      GCONF_CLIENT_PRELOAD_NONE, NULL);

	gfloppy_config_load (&config, client);

	/* if the two FAT formatting backend are present, then the advanced
	   setting choose for us, but we don't have to take the badblocks
	   command in account */
	if (floppy.mkdosfs_cmd != NULL && floppy.mformat_cmd != NULL)
		floppy.mkdosfs_backend = config.prefer_mkdosfs_backend;
	else
		floppy.mkdosfs_backend = floppy.mkdosfs_cmd != NULL;

	/* Sigh, sorry for using a global var for this */
	prefer_mkdosfs_backend = config.prefer_mkdosfs_backend;

	/* Sync UI from config. We don't emit any signal here, since we emit
	   just after this sync the "changed" signal of type_option,
	   which emit all the other necessary signals */
	if (config.default_fs && !strcmp (config.default_fs, GFLOPPY_CONFIG_FS_FAT))
		gtk_option_menu_set_history (GTK_OPTION_MENU (type_option), GFLOPPY_FAT);
	else
		gtk_option_menu_set_history (GTK_OPTION_MENU (type_option), GFLOPPY_E2FS);

	g_free (config.default_fs);

	l = gtk_radio_button_get_group (GTK_RADIO_BUTTON (quick_format_rb));
	default_mode_rb = (GtkWidget *) g_slist_nth (l, GFLOPPY_FORMAT_THOROUGH - config.default_formatting_mode)->data;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (default_mode_rb), TRUE);

	/* bold header labels per HIG; done here to avoid markup in translations */
	label = glade_xml_get_widget (xml, "physical_label");
	label_text = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", gtk_label_get_text (GTK_LABEL (label)));
	gtk_label_set_markup (GTK_LABEL (label), label_text);
	g_free (label_text);

	label = glade_xml_get_widget (xml, "filesystem_label");
	label_text = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", gtk_label_get_text (GTK_LABEL (label)));
	gtk_label_set_markup (GTK_LABEL (label), label_text);
	g_free (label_text);

	label = glade_xml_get_widget (xml, "formatting_label");
	label_text = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", gtk_label_get_text (GTK_LABEL (label)));
	gtk_label_set_markup (GTK_LABEL (label), label_text);
	g_free (label_text);

	/* check the UI consistency for a couple of settings */
	g_signal_emit_by_name (type_option, "changed", NULL);

	/* init_commands checks for at least one of the mke2fs or mkdosfs/mformat commands */
	if (!floppy.mke2fs_cmd || !(floppy.mkdosfs_cmd || floppy.mformat_cmd)) {
		GtkWidget *menu;
		GtkWidget *menuitem;
		GtkWidget *label;
		const char *text;

		/* Well, is there's any proper solution ? */
		menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (type_option));
		menuitem = GTK_WIDGET (g_list_nth (GTK_MENU_SHELL (menu)->children, floppy.mke2fs_cmd ? 0 : 1)->data);
		text = gtk_label_get_text (GTK_LABEL (GTK_BIN (menuitem)->child));

		fs_entry = glade_xml_get_widget (xml, "fs_entry");
		gtk_entry_set_text (GTK_ENTRY (fs_entry), text);

		gtk_option_menu_set_history (GTK_OPTION_MENU (type_option), floppy.mke2fs_cmd ? 1 : 0);

		gtk_widget_hide (type_option);
		gtk_widget_show (fs_entry);
		
		label = glade_xml_get_widget (xml, "type_label");
		gtk_label_set_mnemonic_widget (GTK_LABEL(label), GTK_WIDGET(fs_entry));
	}

	init_devices (ctx);

	gtk_widget_show (GTK_WIDGET (toplevel));

	gtk_main ();

	/* Sync config from UI before saving it */
	switch (gtk_option_menu_get_history (GTK_OPTION_MENU (type_option))) {
		case GFLOPPY_E2FS:
			config.default_fs = GFLOPPY_CONFIG_FS_EXT2;
			break;

		case GFLOPPY_FAT:
			config.default_fs = GFLOPPY_CONFIG_FS_FAT;
			break;
	}

	config.default_formatting_mode = get_selected_formatting_mode ();

	gfloppy_config_save (&config, client);

	g_object_unref (G_OBJECT (client));

#ifdef USE_HAL
	if (ctx != NULL) {
		DBusError derror;
		libhal_ctx_shutdown ((LibHalContext *)ctx, &derror);
		if (dbus_error_is_set (&derror))
			dbus_error_free (&derror);

		libhal_ctx_free ((LibHalContext *)ctx);
	}
#endif

	g_object_unref (program);
	return 0;
}

