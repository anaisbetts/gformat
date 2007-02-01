#ifndef GFLOPPY_FDFORMAT_H_
#define GFLOPPY_FDFORMAT_H_

#include "gfloppy.h"

typedef enum {
	MSG_PROGRESS = 'P',
	MSG_MESSAGE = 'M',
	MSG_ERROR = 'E',
	MSG_BADBLOCK = 'B'
} MessageType;

void fd_print (GFloppy *floppy, MessageType type, char *string);

gint fdformat_disk (GFloppy *floppy);

#endif
