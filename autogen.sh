#!/bin/sh

aclocal
libtoolize --force --copy
automake -a
autoconf
USE_GNOME2_MACROS=1 USE_COMMON_DOC_BUILD=yes . gnome-autogen.sh
