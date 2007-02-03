## Process this file with automake to produce Makefile.in

INCLUDES = \
	-DGNOMELOCALEDIR=\""$(gnomeutilslocaledir)"\"	\
        -DGLADEDIR=\""$(gladedir)"\"           	\
        -DDATADIR=\""$(datadir)"\"		\
	-I. -Wall				\
	$(GFORMAT_CFLAGS)			

bin_PROGRAMS = gnome-format

AM_CFLAGS = -std=c99

gladedir = $(pkgdatadir)/glade
glade_DATA = gformat.glade

gnome_format_SOURCES = \
	device-info.c \
	format-dialog.c \
	logger.c \
	main.c \
	partutil.c

gnome_format_LDADD = $(GFORMAT_LIBS) 

gnome_format_LDFLAGS = -export-dynamic

man_MANS = gnome-format.1

EXTRA_DIST = 		     \
	gformat2.glade		\
	gformat.1
