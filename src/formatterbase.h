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

#include "partutil.h"

#define FORMATTER_DONT_SET_PARTITION 		-1

/* Forward Declaration */
typedef struct _Formatter Formatter;

/* FormatterOps declarations */
typedef gboolean (*FormatterCanFormat) 	(Formatter* this, const char* blockdev, const char* fs);
typedef gboolean (*FormatterDoFormat) 	(Formatter* this, 
					 const char* blockdev, 
					 const char* fs, 
					 int partition_number, 	/* Partition number or FORMATTER_DONT_SET_PARTITION */
					 GHashTable* options,
					 GError** error);
typedef PartitionScheme (*FormatterTableHint)  	(Formatter* this, const char* blockdev, const char* fs);
typedef void 	 (*FormatterUnref) 	(Formatter* this);
/* FormatterClientOps declarations */
typedef void 	 (*FormatterSetText) 		(Formatter* this, const gchar* text);
typedef void 	 (*FormatterSetProgress) 	(Formatter* this, gdouble progress);

/* These functions are implemented by the formatter module */
typedef struct _FormatterOps 
{
	FormatterCanFormat can_format;		/* Optional */
	FormatterTableHint table_hint;		/* Optional */
	FormatterUnref unref;			/* Optional */
	FormatterDoFormat do_format;		/* Mandatory */
} FormatterOps;

/* These functions are implemented by the caller and are called by
 * the specific formatter backend */
typedef struct _FormatterClientOps
{
	FormatterSetText set_text;		/* Optional */
	FormatterSetProgress set_progress;	/* Optional */
} FormatterClientOps;

struct _Formatter
{
	const char** available_fs_list; 	/* Null-terminated array of const char ptrs */
	FormatterOps fops;
	FormatterClientOps fcops;
	gpointer client_data;			/* This is reserved for the client */
	gpointer user_data;			/* This is reserved for the formatter backend */
};

void formatter_set_client_ops(GSList* formatter_list, FormatterClientOps ops);
void formatter_set_client_data(GSList* formatter_list, gpointer client_data);
gboolean formatter_can_format(GSList* formatter_list, const char* fs, const char* blockdev);
PartitionScheme formatter_table_hint (GSList* formatter_list, const char* blockdev, const char* fs);
gboolean formatter_do_format(GSList* formatter_list, 
			     const char* blockdev, 
			     const char* fs, 
			     int partition_number,
			     GHashTable* options, 
			     GError** error);
void formatter_list_free(GSList* formatter_list);
void formatter_client_set_text(Formatter* this, const gchar* text);
void formatter_client_set_progress(Formatter* this, gdouble progress);

int get_partnum_from_blockdev(const char* blockdev);

#endif
