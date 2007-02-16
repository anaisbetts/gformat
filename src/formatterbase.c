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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include "formatterbase.h"

gboolean
default_formatter_can_format(Formatter* fmtr, const char* blockdev, const char* fs)
{
	g_assert(fs != NULL);

	/* Just do a simple scan and see if we find a match */
	const char* iter;
	for(iter = *fmtr->available_fs_list; iter != NULL; iter++) {
		if(!strcmp(iter, fs))
			return TRUE;
	}
	return FALSE;
}


/*
 * Public Functions
 */

void 
formatter_set_client_ops(GSList* formatter_list, FormatterClientOps ops)
{
	GSList* iter = formatter_list;
	for(iter = formatter_list; iter != NULL; iter = g_slist_next(iter)) {
		Formatter* current = iter->data;
		current->fcops = ops;
	}
}

gboolean
formatter_can_format(GSList* formatter_list, const char* fs, const char* blockdev)
{
	GSList* iter = formatter_list;
	for(iter = formatter_list; iter != NULL; iter = g_slist_next(iter)) {
		Formatter* current = iter->data;
		FormatterCanFormat can_format = (current->fops.can_format ? 
						 current->fops.can_format : 
						 default_formatter_can_format);

		if( (*can_format)(current, fs, blockdev))
			return TRUE;
	}
	return FALSE;
}

gboolean
formatter_do_format(GSList* formatter_list, 
		    const char* blockdev, 
		    const char* fs, 
		    gboolean set_partition_table,
		    GHashTable* options, 
		    GError** error)
{
	GSList* iter = formatter_list;
	for(iter = formatter_list; iter != NULL; iter = g_slist_next(iter)) {
		Formatter* current = iter->data;
		FormatterCanFormat can_format = (current->fops.can_format ? 
						 current->fops.can_format : 
						 default_formatter_can_format);

		if( !(*can_format)(current, fs, blockdev) )
			continue;
		g_assert(current->fops.do_format != NULL);
		return (*(current->fops.do_format))(current, blockdev, fs, set_partition_table, options, error);
	}

	return FALSE;
}

void 
formatter_list_free(GSList* formatter_list)
{
	GSList* iter = formatter_list;
	for(iter = formatter_list; iter != NULL; iter = g_slist_next(iter)) {
		Formatter* current = iter->data;
		if(current->fops.unref)
			(*(current->fops.unref))(current);
		g_free(current);
	}
	g_slist_free(formatter_list);
}

void 
formatter_client_set_text(Formatter* this, const gchar* text)
{
	if(this->fcops.set_text)
		(*(this->fcops.set_text))(this, text);
}

void 
formatter_client_set_progress(Formatter* this, gdouble progress)
{
	if(this->fcops.set_progress)
		(*(this->fcops.set_progress))(this, progress);
}

int
get_partnum_from_blockdev(const char* blockdev)
{
	g_assert(blockdev != NULL);
	if(!blockdev)	return -1;

	int i;
	gboolean one_seen = FALSE;
	for(i = strlen(blockdev)-1; i >= 0; i++) {
		if(blockdev[i] <= '0' || blockdev[i] >= '9')	break;
		one_seen = TRUE;
	}

	if(!one_seen)	return -1;
	return atoi(blockdev+i);
}
