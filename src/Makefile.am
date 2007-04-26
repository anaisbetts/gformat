## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DGNOMELOCALEDIR=\""$(gnomeutilslocaledir)"\"	\
        -DGLADEDIR=\""$(gladedir)"\"           	\
        -DDATADIR=\""$(datadir)"\"		\
	-DUSE_PARTED 				\
	-I. -Wall				\
	$(GFORMAT_CFLAGS)			

bin_PROGRAMS = gnome-format

AM_CFLAGS = -std=c99

gladedir = $(pkgdatadir)/glade
glade_DATA = gformat.glade

gnome_format_SOURCES = \
	device-info.c 		\
	format-dialog.c 	\
	formattify.c 		\
	logger.c 		\
	main.c 			\
	partutil.c

THEHEADERS = 

noinst_HEADERS = \
	device-info.h 		\
	formattify.h 		\
	format-dialog.h 	\
	logger.h 		\
	partutil.h

gnome_format_LDADD = $(GFORMAT_LIBS) 

gnome_format_LDFLAGS = -export-dynamic -lparted

man_MANS = gnome-format.1

EXTRA_DIST = 		     \
	gformat.glade		\
	gnome-format.1
