/* gfloppy-config.c
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

#include <glib.h>
#include <string.h>
#include <gconf/gconf-client.h>

#include "gfloppy-config.h"
#include "gfloppy.h"

static gboolean check_gconf_error (GError **error);

static gboolean
check_gconf_error (GError **error)
{
	g_return_val_if_fail (error != NULL, FALSE);

	if (*error != NULL) {
		g_warning ("GConf error: %s", (*error)->message);
		g_error_free (*error);
		*error = NULL;

		return FALSE;
	}		

	return TRUE;
}

void
gfloppy_config_load (GFloppyConfig *config,
		     GConfClient   *client)
{
	GError *error = NULL;

	g_return_if_fail (config != NULL);
	g_return_if_fail (client != NULL);

	/* We'll default the value when analyzing it, after loading the config */
	config->default_fs = g_strdup (gconf_client_get_string (client, "/apps/gfloppy/default_fs", &error));
	check_gconf_error (&error);

        /* Check to make sure that it's a valid string */
        if (config->default_fs == NULL)
		config->default_fs = g_strdup (GFLOPPY_CONFIG_FS_FAT);
        else if (config->default_fs && strcmp (config->default_fs, GFLOPPY_CONFIG_FS_EXT2) && strcmp (config->default_fs, GFLOPPY_CONFIG_FS_FAT)) {
		g_free (config->default_fs);
		config->default_fs = g_strdup (GFLOPPY_CONFIG_FS_FAT);
        }

	config->default_formatting_mode = gconf_client_get_int (client, "/apps/gfloppy/default_formatting_mode", &error);

	/* We need to care about users sillyness ;) */
	if (!check_gconf_error (&error) ||
	    config->default_formatting_mode < GFLOPPY_FORMAT_QUICK ||
	    config->default_formatting_mode > GFLOPPY_FORMAT_THOROUGH)
		config->default_formatting_mode = GFLOPPY_FORMAT_STANDARD;

	config->prefer_mkdosfs_backend = gconf_client_get_bool (client, "/apps/gfloppy/prefer_mkdosfs_backend", &error);

	if (!check_gconf_error (&error))
		config->prefer_mkdosfs_backend = TRUE;
}

void
gfloppy_config_save (GFloppyConfig *config,
		     GConfClient   *client)
{
	GError *error = NULL;

	g_return_if_fail (config != NULL);
	g_return_if_fail (client != NULL);

	if (gconf_client_key_is_writable (client, "/apps/gfloppy/default_fs", &error))
		gconf_client_set_string (client, "/apps/gfloppy/default_fs", config->default_fs, &error);
	check_gconf_error (&error);

	if (gconf_client_key_is_writable (client, "/apps/gfloppy/default_formatting_mode", &error))
		gconf_client_set_int (client, "/apps/gfloppy/default_formatting_mode", config->default_formatting_mode, &error);
	check_gconf_error (&error);
}

void gfloppy_config_free (GFloppyConfig *config)
{
	g_return_if_fail (config != NULL);

	g_free (config->default_fs);
	g_free (config);
}
