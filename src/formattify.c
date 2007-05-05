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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>

#include <parted/parted.h>

#include "device-info.h"
#include "format-dialog.h"
#include "formattify.h"

/* TODO: Put this into configure.in */
#ifndef FORMAT_SCRIPT_DIR
#define FORMAT_SCRIPT_DIR "./scripts"
#endif



/*
 * Process-spawning functions
 */

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
	ProcessOutput* cb_data = g_new0(ProcessOutput, 1);
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
		g_warning("Spawn async failed!\n");
		return FALSE;
	}
	g_debug("Out fd=%d, Err fd=%d", out_fd, error_fd);

	packed_data->real_cb = callback; 	packed_data->real_userdata = user_data;
	packed_data->out_fd = out_fd; 	packed_data->error_fd = error_fd;

	g_child_watch_add(pid, spawn_cb, packed_data);
	return TRUE;
}

void 
process_output_free(ProcessOutput* obj)
{
	g_assert(obj != NULL);
	if(obj->stderr_output)
		g_free(obj->stderr_output);
	if(obj->stdout_output)
		g_free(obj->stdout_output);
	g_free(obj);
}


/*
 * High-level script functions
 */

static gboolean
add_cb(gpointer data)
{
	g_assert(data != NULL);

	ProcessOutput* output = data;
	GHashTable* table = output->user_data;

}

static gboolean
add_supported_fs(const char* script_path, GHashTable* hash)
{
	gchar* cmd[] = {"", "--capabilities", NULL};
	gchar *out, *err;
	gint status;
	cmd[0] = script_path;

	if(!g_spawn_sync(NULL, cmd, NULL, 0, NULL, NULL, &out, &err, &status, NULL))
		return FALSE;

	if(!out)
		return FALSE;

	/* Parse a list of supported filesystems in the format:
	 *
	 * ext2
	 * /usr/sbin/mkfs.ext2
	 * reiserfs
	 * /usr/sbin/reiserfscreate
	 * etc... 
	 */
	gchar** split = g_strsplit(out, "\n", 0/*all tokens*/);
	gchar** iter = split;
	while(iter[0] && iter[1]) {
		g_debug("Adding %s, %s", iter[0], iter[1]);
		g_hash_table_insert(hash, g_strdup(iter[0]), g_strdup(iter[1]));
		iter += 2;
	}
	
	return TRUE;
}

static void g_free_cb(gpointer data) { if(data) g_free(data); }

GHashTable* 
build_supported_fs_list(void)
{
	GHashTable* hash = g_hash_table_new_full(g_str_hash, g_str_equal, g_free_cb, g_free_cb);
	GDir* dir = g_dir_open(FORMAT_SCRIPT_DIR, 0, NULL);
	if(!dir) {
		g_error("Formatting scripts path '%s' doesn't exist!", FORMAT_SCRIPT_DIR);
		return NULL;
	}

	gchar* path;
	const gchar* file = g_dir_read_name(dir);
	while(file != NULL) {
		path = g_build_filename(FORMAT_SCRIPT_DIR, file, NULL);
		g_debug("path = %s, file = %s, FORMAT_SCRIPT_DIR = %s", path, file, FORMAT_SCRIPT_DIR);

		if(!add_supported_fs(path, hash))
			g_warning(_("Error in script: '%s'"), path);
		g_free(path);

		file = g_dir_read_name(dir);
	}
	g_dir_close(dir);

	g_debug("Filesystem list has %d entries", g_hash_table_size(hash));

	return hash;
}


/*
 * Format idle-loop tasks
 */

gboolean 
do_mkfs(gpointer data)
{
	g_assert(data != NULL);
	FormatDialog* dialog = data;

	/* Figure out which script to run */
	//gchar* fs_name = 
	//gchar* fs_script = dialog->fs_map
}
