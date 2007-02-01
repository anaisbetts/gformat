/* gfloppy.c
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

#include <glib/gi18n.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <unistd.h>

#ifdef HAVE_LINUX_FD_H
#include <linux/fd.h>
#include <linux/fs.h>
#endif

#include <string.h>
#include "gfloppy.h"
#include "fdformat.h"
#include "badblocks.h"

extern gboolean prefer_mkdosfs_backend;

typedef enum {
	CMD_MKE2FS = 0,
	CMD_MKDOSFS,
	CMD_MFORMAT	
} FSCreationCmdType;

static gboolean write_badblocks_file    (GFloppy *floppy, char **bb_filename);
static void     badblocks_check_cleanup (GFloppy *floppy, char  *bb_filename);

static int      execute_fs_creation_cmd (GFloppy *floppy,
					 FSCreationCmdType type,
					 const char *bb_filename);

gint
floppy_block_size (GFloppySize size)
{
	gint rc;

	switch (size) {
	case 0:
		/* 1.44M */
		rc = 2880;
		break;
	case 1:
		/* 1.2M */
		rc = 2160;
		break;
	case 2:
		/* 720k */
		rc = 1440;		break;
	case 3:
		/* 360k */
		rc = 720;
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	return rc;
}

static GPtrArray *
make_mformat_cmd_args (GFloppy *floppy)
{
	GPtrArray *args;
	char *geometry_args[4][3] = {
		{ "80", "2", "18" }, /* 1.44 Mo */
		{ "80", "2", "15" }, /*  1.2 Mo */
		{ "80", "2",  "9" }, /*  720 Ko */
		{ "40", "2",  "9" }, /*  360 Ko */
	};

	if (floppy->size < 0 || floppy->size > 3)
		g_assert_not_reached ();

	args = g_ptr_array_new ();
	g_ptr_array_add (args, floppy->mformat_cmd);

	if (floppy->volume_name) {
		g_ptr_array_add (args, "-v");
		g_ptr_array_add (args, floppy->volume_name);
	}

	g_ptr_array_add (args, "-t");
	g_ptr_array_add (args, geometry_args[floppy->size][0]);

	g_ptr_array_add (args, "-h");
	g_ptr_array_add (args, geometry_args[floppy->size][1]);

	g_ptr_array_add (args, "-s");
	g_ptr_array_add (args, geometry_args[floppy->size][2]);

	g_ptr_array_add (args, floppy->mdevice);
	g_ptr_array_add (args, NULL);

	return args;
}

static GPtrArray *
make_mkdosfs_cmd_args (GFloppy     *floppy,
		       const char *bb_filename)
{
	GPtrArray *args;

	args = g_ptr_array_new ();
	g_ptr_array_add (args, floppy->mkdosfs_cmd);

	if (bb_filename) {
		g_ptr_array_add (args, "-l");
		g_ptr_array_add (args, (gpointer) bb_filename);
	}

	if (floppy->volume_name) {
		g_ptr_array_add (args, "-n");
		g_ptr_array_add (args, floppy->volume_name);
	}

	if (!g_ascii_isdigit (floppy->device[strlen(floppy->device) - 1]))
		g_ptr_array_add (args, "-I");

	g_ptr_array_add (args, floppy->device);
	g_ptr_array_add (args, NULL);

	return args;
}

static GPtrArray *
make_mke2fs_cmd_args (GFloppy     *floppy,
		      const char *bb_filename)
{
	GPtrArray *args;

	args = g_ptr_array_new ();
	g_ptr_array_add (args, floppy->mke2fs_cmd);

	if (bb_filename) {
		g_ptr_array_add (args, "-l");
		g_ptr_array_add (args, (gpointer) bb_filename);
	}

	if (floppy->volume_name) {
		g_ptr_array_add (args, "-L");
		g_ptr_array_add (args, floppy->volume_name);
	}

	g_ptr_array_add (args, floppy->device);
	g_ptr_array_add (args, NULL);

	return args;

#if 0
	char *retval = NULL;
	char *bad_block_flag;

	if (floppy->quick_format)
		bad_block_flag = "";
	else
		bad_block_flag = "-c ";

	switch (floppy->size) {
	case 0:
		/* 1.44 m */
		retval = g_strconcat (floppy->mke2fs_cmd, " -b 2880 ", bad_block_flag, floppy->device, NULL);
		break;
	case 1:
		/* 1.2 m */
		retval = g_strconcat (floppy->mke2fs_cmd, " -b 2160 ", bad_block_flag, floppy->device, NULL);
		break;
	case 2:
		/* 720 k */
		retval = g_strconcat (floppy->mke2fs_cmd, " -b 1440 ", bad_block_flag, floppy->device, NULL);
		break;
	case 3:
		/* 360 k */
		retval = g_strconcat (floppy->mke2fs_cmd, " -b 720 ", bad_block_flag, floppy->device, NULL);
		break;
	default:
		g_assert_not_reached ();
	}
	return retval;
#endif
}

static gboolean
write_badblocks_file (GFloppy *floppy,
		      char  **bb_filename)
{
	GIOChannel *ioc;
	GIOStatus status;
	GList *l;
	int fd;

	fd = g_file_open_tmp (NULL, bb_filename, NULL);

	if (fd == -1) {
		fd_print (floppy, MSG_ERROR, _("Error while creating a unique filename for the bad blocks list file."));
		return FALSE;
	}

	ioc = g_io_channel_unix_new (fd);

	for (l = floppy->badblocks_list; l; l = l->next) {
		char *buf = g_strdup_printf ("%lu\n", *((unsigned long *) l->data));
		status = g_io_channel_write_chars (ioc, buf, strlen (buf), NULL, NULL);
		g_free (buf);
	}

	g_io_channel_shutdown (ioc, TRUE, NULL);
	g_io_channel_unref (ioc);
	close (fd);

	if (status != G_IO_STATUS_NORMAL) {
		unlink (*bb_filename);
		g_free (*bb_filename);

		fd_print (floppy, MSG_ERROR, _("Error while filling the bad blocks list file."));

		return FALSE;
	}

	return TRUE;
}

static void
badblocks_check_cleanup (GFloppy *floppy,
			 char   *bb_filename)
{
	g_return_if_fail (floppy->formatting_mode == GFLOPPY_FORMAT_THOROUGH);
	g_return_if_fail (floppy->badblocks_list != NULL);

	unlink (bb_filename);
	g_free (bb_filename);

	g_list_foreach (floppy->badblocks_list, (GFunc) g_free, NULL);
	g_list_free (floppy->badblocks_list);
}


static int
execute_fs_creation_cmd (GFloppy           *floppy,
			 FSCreationCmdType type,
			 const char      *bb_filename)
{
	GPtrArray *args;
	GError *error = NULL;
	char *stdout_buf = NULL;
	char *stderr_buf = NULL;
	gint status;

	g_return_val_if_fail (floppy != NULL, -1);

	switch (type) {
		case CMD_MKE2FS:
			args = make_mke2fs_cmd_args (floppy, bb_filename);
			break;
		case CMD_MKDOSFS:
			args = make_mkdosfs_cmd_args (floppy, bb_filename);
			break;
		case CMD_MFORMAT:
			args = make_mformat_cmd_args (floppy);
			break;
		default:
			g_assert_not_reached ();
	}

	/* Note: the stdout_buf can be thought as unuseful, but it avoids a
	 *	 race condition: without any output descriptor, g_spawn_*
	 *	 uses the parent process output descriptor, which is already
	 *	 closed */

	if (!g_spawn_sync (NULL,
			   (char **) args->pdata, NULL,
			   G_SPAWN_SEARCH_PATH,
			   NULL, NULL,
			   &stdout_buf, &stderr_buf,
			   &status, &error)) {
		char *msg;

		msg = g_strdup_printf (_("Error while spawning the (%s) command: %s."), (char *)args->pdata[0], error->message);
		fd_print (floppy, MSG_ERROR, msg);
		g_free (msg);

		g_error_free (error);

		g_ptr_array_free (args, FALSE);

		return -1;
	}

	g_free (stdout_buf);

	if (stderr_buf != NULL) {
		char *err_output = stderr_buf;
		char *msg;
		gboolean has_real_err_output = TRUE;

		/* The first line is skipped for mke2fs because it contains the
		   application name, its version and the compile date. Note this
		   is displayed on stdout for mkdosfs. That's rather strange. */

		if (type == CMD_MKE2FS) {
			err_output = strstr (stderr_buf, "\n");

			if (!err_output) {
				fd_print (floppy, MSG_ERROR, _("Unknown mke2fs starting signature, cancelling."));

				g_free (stderr_buf);
				g_ptr_array_free (args, FALSE);

				return -1;
			}

			if (strlen (++err_output) == 0)
				has_real_err_output = FALSE;
		}

		if ((type != CMD_MKE2FS || has_real_err_output) &&
		     strlen (err_output) > 0) {
			msg = g_strdup_printf (_("The filesystem creation utility (%s) reported the following errors:\n\n%s (%d)"),
					       (char *)args->pdata[0], err_output, (int)(strlen (err_output))); // FIXME:
			fd_print (floppy, MSG_ERROR, msg);
			g_free (msg);

			g_free (stderr_buf);
			g_ptr_array_free (args, FALSE);

			return -1;
		}
	}

	g_ptr_array_free (args, FALSE);

	if (WIFEXITED (status))
		return WEXITSTATUS (status);

	fd_print (floppy, MSG_ERROR, _("Abnormal child process termination."));

	return -1;
}

static int
execute_mbadblocks (GFloppy *floppy)
{
	GError *error = NULL;
	char *args[3];
	char *stdout_buf = NULL, *stderr_buf = NULL;
	gint status;

	args[0] = floppy->badblocks_cmd;
	args[1] = floppy->mdevice;
	args[2] = NULL;

	if (!g_spawn_sync (NULL,
			   args, NULL,
			   G_SPAWN_SEARCH_PATH,
			   NULL, NULL,
			   &stdout_buf, &stderr_buf,
			   &status, &error)) {
		char *msg;

		msg = g_strdup_printf (_("Error while spawning the mbadblocks command: %s."), error->message);
		fd_print (floppy, MSG_ERROR, msg);
		g_free (msg);

		g_error_free (error);

		return -1;
	}

	if (stderr_buf != NULL) {
		char *msg;

		msg = g_strdup_printf (_("The bad blocks checking utility (mbadblocks) reported the following errors:\n%s."), stderr_buf);
		fd_print (floppy, MSG_ERROR, msg);
		g_free (msg);

		g_free (stderr_buf);
	}

	if (stdout_buf != NULL) {
		char *needle = "Bad cluster ";
		char *p = stdout_buf;
		unsigned long cluster;

		while (1) {
			p = strstr (p, needle);

			if (!p)
				break;

			if (sscanf (p + strlen (needle), "%lu", &cluster) == 1) {
				char *msg;

				/* we don't add it to the badblocks list since
				   mformat doesn't accept a list of bad blocks
				   like the mk*fs tools */

				msg = g_strdup_printf ("%lu", cluster);
	                        fd_print (floppy, MSG_BADBLOCK, msg);
	                        g_free (msg);
			} else
				g_warning (("Invalid mbadblocks output, trying to continue."));

			/* we could also skip the "%lu found." text, but if the
			   output is invalid, that wouldn't really help either.
			   The code is really sexier that way. */
			p = p + strlen (needle);
		}

		g_free (stdout_buf);
	}

	if (WIFEXITED (status))
		return WEXITSTATUS (status);

	fd_print (floppy, MSG_ERROR, _("Abnormal mbadblocks child process termination."));

	return -1;
}

static int
format_ext2fs (GFloppy *floppy)
{
	gint rc = 0;
	char *bb_filename = NULL;

	g_return_val_if_fail (floppy != NULL, -1);

	/* checks for bad_blocks */
	if (floppy->formatting_mode == GFLOPPY_FORMAT_THOROUGH) {
		rc = check_badblocks (floppy);

		if (rc != 0)
			return -1;

		if (floppy->badblocks_list)
			if (!write_badblocks_file (floppy, &bb_filename))
				return -1;
	}

	/* make the filesystem */
	fd_print (floppy, MSG_MESSAGE, _("Making filesystem on disk..."));
	rc = execute_fs_creation_cmd (floppy, CMD_MKE2FS, bb_filename);

	if (floppy->formatting_mode == GFLOPPY_FORMAT_THOROUGH &&
	    floppy->badblocks_list)
		badblocks_check_cleanup (floppy, bb_filename);

	if (rc > 3) {
		fd_print (floppy, MSG_ERROR, _("Unable to create filesystem correctly."));

		return -1;
	}

	fd_print (floppy, MSG_MESSAGE, _("Making filesystem on disk... Done"));
	fd_print (floppy, MSG_PROGRESS, "100");

	return rc;
}

static int
format_fat (GFloppy *floppy)
{
	gint rc = 0;
	char *bb_filename = NULL;

	g_return_val_if_fail (floppy != NULL, -1);

	if (floppy->mkdosfs_backend &&
	    floppy->formatting_mode == GFLOPPY_FORMAT_THOROUGH) {
		rc = check_badblocks (floppy);

		if (rc != 0)
			return -1;

		if (floppy->badblocks_list)
			if (!write_badblocks_file (floppy, &bb_filename))
				return -1;
	}

	/* make the filesystem */
	fd_print (floppy, MSG_MESSAGE, _("Making filesystem on disk..."));

	if (floppy->mkdosfs_backend)
		rc = execute_fs_creation_cmd (floppy, CMD_MKDOSFS, bb_filename);
	else
		rc = execute_fs_creation_cmd (floppy, CMD_MFORMAT, NULL);

	if (floppy->mkdosfs_backend &&
	    floppy->formatting_mode == GFLOPPY_FORMAT_THOROUGH &&
	    floppy->badblocks_list)
		badblocks_check_cleanup (floppy, bb_filename);

	fd_print (floppy, MSG_MESSAGE, _("Making filesystem on disk... Done"));

	/* check for bad blocks if using mbadblocks is reversed */
	if (!floppy->mkdosfs_backend && /* the mbadblocks command presence is already checked */
	    floppy->formatting_mode == GFLOPPY_FORMAT_THOROUGH) {
		fd_print (floppy, MSG_MESSAGE, _("Checking for bad blocks... (this might take a while)")); 

		rc = execute_mbadblocks (floppy);
		
		if (rc != 0) {
			fd_print (floppy, MSG_ERROR, _("Error while checking the bad blocks."));
			return -1;
		}

		fd_print (floppy, MSG_MESSAGE, _("Checking for bad blocks... Done"));
		fd_print (floppy, MSG_PROGRESS, "100");

		return 0;
	}

	fd_print (floppy, MSG_PROGRESS, "100");
	
	return rc;
}

void
format_floppy (GFloppy *floppy)
{
	gint rc = 0;

	fd_print (floppy, MSG_PROGRESS, "000");

	/* low-level format */
	if (floppy->formatting_mode != GFLOPPY_FORMAT_QUICK) {
		rc = fdformat_disk (floppy);

		if (rc != 0)
			_exit (rc);
	}

	if (floppy->type == GFLOPPY_E2FS)
		rc = format_ext2fs (floppy);
	else
		rc = format_fat (floppy);

	_exit (rc);
}

GFloppyStatus
test_floppy_device (char *device)
{
	struct stat s;
        struct floppy_drive_struct ds;
        char name[32];
	gint fd;
	GFloppyStatus retval;

	/* sanity check */
	if (device == NULL || *device == '\000')
		return GFLOPPY_NO_DEVICE;

	if (stat (device, &s) < 0)
		return GFLOPPY_NO_DEVICE;
	if (!S_ISBLK(s.st_mode))
		return GFLOPPY_NO_DEVICE;

	if (access (device, R_OK|W_OK) != 0)
		return GFLOPPY_INVALID_PERMISSIONS;

	fd = open (device, O_RDONLY|O_NONBLOCK);
	if (fd < 0)
		return GFLOPPY_NO_DEVICE;

	ioctl (fd, FDRESET, NULL);
	if (ioctl (fd, FDGETDRVTYP, name) == 0) {
		if (name && strcmp(name,"(null)")) {
			if (ioctl(fd, FDPOLLDRVSTAT, &ds) == 0 && ds.track >= 0)
				retval = GFLOPPY_DEVICE_OK;
			else
				retval = GFLOPPY_DEVICE_DISCONNECTED;
		} else {
			retval = GFLOPPY_NO_DEVICE;
		}
	} else {
		/* For usb floppies */
		retval = GFLOPPY_DEVICE_OK;
	}
	close(fd);

	return retval;
}
