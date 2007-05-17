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

#ifndef _FORMATTIFY_H
#define _FORMATTIFY_H

#include "format-dialog.h"

typedef struct _ProcessOutput
{
	GSourceFunc callback;
	gchar* stdout_output;
	gchar* stderr_output;
	int ret;
	gpointer user_data;
} ProcessOutput;

gboolean spawn_async_get_output(gchar** argv, GSourceFunc callback, gpointer user_data);
void process_output_free(ProcessOutput* obj);
GHashTable* build_supported_fs_list(void);
gboolean do_mkfs(FormatDialog* dialog, const char* block_device);

#endif
