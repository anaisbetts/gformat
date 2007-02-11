/*
 * formatterbase.c - Define a modular system for formatting filesystems
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

#ifndef _FORMATTERBASE_H
#define _FORMATTERBASE_H

/* Forward Declaration */
typedef struct _Formatter Formatter;

typedef gboolean (*FormatterCanFormat) 	(Formatter* this, const char* blockdev, const char* fs);

typedef gboolean (*FormatterDoFormat) 	(Formatter* this, 
					 const char* blockdev, 
					 const char* fs, 
					 GHashTable* options,
					 GError** error);

typedef void 	 (*FormatterUnref) 	(Formatter* this);

typedef struct _FormatterOps 
{
	FormatterCanFormat can_format;
	FormatterDoFormat do_format;
	FormatterUnref unref;
} FormatterOps;

struct _Formatter
{
	const char** available_fs_list; 	/* Null-terminated array of const char ptrs */
	FormatterOps fops;
	gpointer user_data;
};


gboolean formatter_can_format(GSList* formatter_list, const char* fs, const char* blockdev);
gboolean formatter_do_format(GSList* formatter_list, const char* blockdev, const char* fs, GHashTable* options, GError** error);
void formatter_list_free(GSList* formatter_list);

#endif
