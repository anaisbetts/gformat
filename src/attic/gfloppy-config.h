/* gfloppy-config.h
 *
 * Copyright (C) 2002 Stephane Demurget <demurgets@free.fr>
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

#ifndef __GFLOPPY_CONFIG_H__
#define __GFLOPPY_CONFIG_H__

#include <glib.h>
#include <gconf/gconf-client.h>

#define GFLOPPY_CONFIG_FS_EXT2 "ext2"
#define GFLOPPY_CONFIG_FS_FAT  "fat"

typedef struct _GFloppyConfig GFloppyConfig;
struct _GFloppyConfig {
	gchar   *default_fs;              /* Last fs type used */
	gint     default_formatting_mode; /* Last formatting mode used */
	gboolean prefer_mkdosfs_backend;  /* Use mkdosfs as the preferred FAT formatting backend */
};

void gfloppy_config_load (GFloppyConfig *config, GConfClient *client);
void gfloppy_config_save (GFloppyConfig *config, GConfClient *client);

#endif
