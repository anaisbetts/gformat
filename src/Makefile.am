## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DGNOMELOCALEDIR=\""$(gnomeutilslocaledir)"\"	\
        -DGLADEDIR=\""$(gladedir)"\"           	\
        -DDATADIR=\""$(datadir)"\"		\
	-I. -Wall				\
	$(GFLOPPY_CFLAGS)			\
	$(LIBGLADE_CFLAGS)			\
	$(HAL_CFLAGS)

bin_PROGRAMS = gformat

AM_CFLAGS = -std=c99

gladedir = $(pkgdatadir)/glade
glade_DATA = gformat.glade

gformat_SOURCES = \
	device-info.c \
	format-dialog.c \
	logger.c \
	main.c \
	partutil.c

gformat_LDADD = $(GFLOPPY_LIBS) ${LIBGLADE_LIBS} $(HAL_LIBS) 

gformat_LDFLAGS = -export-dynamic

man_MANS = gformat.1

EXTRA_DIST = 		     \
	gformat2.glade		\
	gformat.1
