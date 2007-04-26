/*
 * formattify.c - Perform the actual format, as well as utility functions
 * 		  to spawn processes and capture their output
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

#include <glib.h>
#include <glib/gi18n.h>

#include <parted/parted.h>

#include "device-info.h"
#include "formattify.h"

struct _spawn_cb_pack {
	GFunc real_cb;
	gpointer real_userdata;
	gint out_fd;
	gint error_fd;
};

static void 
spawn_cb(GPid pid, gint status, gpointer data)
{
	struct _spawn_cb_pack* s = data;
	GProcessOutput* cb_data = g_new0(GProcessOutput, 1);
	gsize dontcare;

	/* Pull the data out of the pipes, then pack it in our return data
	 * function */
	GIOChannel* out = g_io_channel_unix_new(s->out_fd);
	GIOChannel* err = g_io_channel_unix_new(s->error_fd);
	g_io_channel_read_to_end(out, &cb_data->stdout_output, &dontcare, NULL);
	g_io_channel_read_to_end(err, &cb_data->stderr_output, &dontcare, NULL);

	/* Clean up and enqueue the idle function */
	cb_data->callback = s->real_cb; 	cb_data->ret = status;
	cb_data->user_data = s->real_userdata;
	g_io_channel_shutdown(out, FALSE, NULL); 	g_io_channel_shutdown(err, FALSE, NULL);
	g_spawn_close_pid(pid);
	g_idle_add(cb_data->callback, cb_data);

	/* Free data */
	g_free(data);
}

gboolean 
spawn_async_get_output(gchar** argv, GSourceFunc callback, gpointer user_data)
{
	GPid pid;
	gint out_fd;
	gint error_fd;
	struct _spawn_cb_pack* packed_data = g_new0(struct _spawn_cb_pack, 1);

	if(!g_spawn_async_with_pipes(NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL, user_data, &pid, 
				     NULL, &out_fd, &error_fd, NULL)) {
		return FALSE;
	}
	g_debug("Out fd=%d, Err fd=%d", out_fd, error_fd);

	packed_data->real_cb = callback; 	packed_data->real_userdata = user_data;
	packed_data->out_fd = out_fd; 	packed_data->error_fd = error_fd;

	g_child_watch_add(pid, spawn_cb, packed_data);
	return TRUE;
}
