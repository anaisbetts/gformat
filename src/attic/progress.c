/* progress.c
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

#include "config.h"
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <string.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include "progress.h"

#define BUFSIZE 256
static char *error_label=NULL;
static GtkMessageType dialog_type=GTK_MESSAGE_ERROR;
static GtkWidget *progress_dialog=NULL;
static GtkWidget *msg_label=NULL;
static GtkWidget *progressbar=NULL;
static gboolean show_error_dialog;

static void
read_stdinput (gpointer data,
	       gint source,
	       GdkInputCondition condition)
{
	int size=0;
	static char buf[BUFSIZE];
	static int pos=0;
	char curbuf[BUFSIZE];
	long int val;
	GFloppy *floppy = (GFloppy *)data;

	gdk_threads_enter ();

	if (condition != GDK_INPUT_READ)
		return;

	while ((size = read(source,curbuf,MIN (BUFSIZE-1, BUFSIZE-pos-1)))>0) {
		int  i;

		/* append onto existing buffer */
		memcpy (buf+pos, curbuf, size);
		pos += size;

		while (1) {
 
			/* scan to see if we have a new message */
			for (i=0; i<pos && buf[i]; i++);
		    
			if ( i == pos )
				break;

			/* found a '\000' so parse message */
			switch (buf[0]) {
			case 'B':
				floppy->nbadblocks++;
				break;
			case 'E':
				show_error_dialog = TRUE;
				error_label = g_strdup (buf+1);
				dialog_type = GTK_MESSAGE_ERROR;
				break;
			case 'M':
				if (msg_label == NULL)
					break;
				gtk_label_set_text (GTK_LABEL (msg_label), buf + 1);
				fflush(stdout);
				break;
			case 'P':
				if (msg_label == NULL)
					break;
				if (strlen (buf) <4)
					break;
				val = strtol (buf + 1, NULL, 10);
				gtk_progress_bar_update (GTK_PROGRESS_BAR (progressbar),
							 ((gfloat) val)/100.0);
				break;
			default:
				if (msg_label == NULL)
					break;
				fflush(stdout);
				gtk_label_set_text (GTK_LABEL (msg_label), buf);
				break;
			}

			/* move remaining data in buffer to start */
			memmove (buf, buf+i+1, pos-i-1);
			pos -= i+1;
		}
	}

	/* zero length read means child exitted */
	if (size == 0) {
		int status;
		int rc;
		GtkWidget *error_dialog;

		wait4 (floppy->pid, &status, WNOHANG, NULL);
		rc = WEXITSTATUS (status);

		/* destroy input handler */
 		gdk_input_remove (floppy->handler_id);
		if (rc == 0 && !WIFSIGNALED (status)) {
			show_error_dialog = TRUE;
	
			if (floppy->nbadblocks > 0) {
				dialog_type = GTK_MESSAGE_WARNING;
				error_label = g_strdup_printf (ngettext ("The floppy has been formatted, but <b>%d bad block</b> (out of %d) has been found and marked.", "The floppy has been formatted, but <b>%d bad blocks</b> (out of %d) have been found and marked.", floppy->nbadblocks), floppy->nbadblocks, floppy->nblocks);
			} else {
				dialog_type = GTK_MESSAGE_INFO;
				error_label = g_strdup (_("Floppy formatted successfully."));
			}
		}

		if (show_error_dialog == FALSE) {
			dialog_type = GTK_MESSAGE_INFO;
			error_label = g_strdup (_("Floppy formatting cancelled."));
		}

		error_dialog = gtk_message_dialog_new ((GtkWindow *)progress_dialog, GTK_DIALOG_MODAL,
						       dialog_type,
						       GTK_BUTTONS_OK,
						       error_label);

		gtk_dialog_set_has_separator (GTK_DIALOG(error_dialog), FALSE);
		gtk_container_set_border_width (GTK_CONTAINER (error_dialog), 5);

		if (rc == 0 && !WIFSIGNALED (status) && floppy->nbadblocks > 0)
                        gtk_label_set_use_markup (GTK_LABEL (GTK_MESSAGE_DIALOG (error_dialog)->label), TRUE);

		/* reset the dialog properties */
		dialog_type = GTK_MESSAGE_INFO;
		g_free (error_label);
		error_label = NULL;

		gtk_dialog_run (GTK_DIALOG (error_dialog));
		gtk_widget_destroy (error_dialog);
		gtk_main_quit ();
	}
	gdk_threads_leave ();
}

static void
progress_response_callback (GtkWidget *progress, gint response, gpointer data)
{
	GFloppy *floppy = (GFloppy *) data;

	kill (floppy->pid, SIGTERM);

	if (response == GTK_RESPONSE_DELETE_EVENT)
		progress_dialog = NULL; /* so the dialog isn't destroyed twice */
}

void
setup_progress_and_run (GFloppy *floppy, GtkWidget *parent)
{
	GtkWidget *vbox;

	show_error_dialog = FALSE;
	progress_dialog = gtk_dialog_new_with_buttons (_("Format Progress"),
						       GTK_WINDOW (parent),
						       GTK_DIALOG_MODAL,
						       GTK_STOCK_CANCEL,
						       GTK_RESPONSE_CANCEL,
						       NULL);

	gtk_container_set_border_width (GTK_CONTAINER (progress_dialog), 5);
	gtk_dialog_set_has_separator (GTK_DIALOG(progress_dialog), FALSE);
					       
	vbox = gtk_vbox_new (FALSE, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 5);
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (progress_dialog)->vbox), vbox,
			    TRUE, TRUE, 0);

	msg_label = gtk_label_new (NULL);
	gtk_misc_set_alignment (GTK_MISC (msg_label), 0.0, 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), msg_label, FALSE, FALSE, 0);
	gtk_widget_show (msg_label);
	progressbar = gtk_progress_bar_new ();
	gtk_box_pack_start (GTK_BOX (vbox), progressbar, FALSE, FALSE, 0);

	floppy->handler_id = gdk_input_add (floppy->message[0], GDK_INPUT_READ, read_stdinput, floppy);

	gtk_signal_connect (GTK_OBJECT (progress_dialog), "response", GTK_SIGNAL_FUNC (progress_response_callback), floppy);

	gtk_widget_show_all (progress_dialog);

	gtk_main ();

	if (progress_dialog) {
		gtk_widget_destroy (progress_dialog);
		progress_dialog = NULL;
	}
}
