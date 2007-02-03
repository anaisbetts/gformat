## Process this file with automake to produce Makefile.in

SUBDIRS = src 


gnome_formatappdir = $(datadir)/applications
gnome_formatapp_in_files = gnome-format.desktop.in
gnome_formatapp_DATA = $(gnome_formatapp_in_files:.desktop.in=.desktop)

#@INTLTOOL_DESKTOP_RULE@
#@INTLTOOL_KEYS_RULE@

schemasdir = $(GCONF_SCHEMA_FILE_DIR)
schemas_in_file = gnome-format.schemas.in
schemas_DATA = $(schemas_in_file:.schemas.in=.schemas)
#@INTLTOOL_SCHEMAS_RULE@

EXTRA_DIST = \
	$(gnome_formatapp_in_files) \
	$(gnome_formatapp_DATA) \
	$(schemas_in_file)

install-data-local: gnome-format.desktop install-misc-local install-gconf-schemas

install-misc-local:
	@$(NORMAL_INSTALL)
	if test -d $(srcdir)/pixmaps; then \
	  $(mkinstalldirs) $(DESTDIR)@PACKAGE_PIXMAPS_DIR@; \
	  for pixmap in $(srcdir)/pixmaps/*; do \
	    if test -f $$pixmap; then \
	      $(INSTALL_DATA) $$pixmap $(DESTDIR)@PACKAGE_PIXMAPS_DIR@; \
	    fi \
	  done \
	fi

dist-hook:
	if test -d pixmaps; then \
	  mkdir $(distdir)/pixmaps; \
	  for pixmap in pixmaps/*; do \
	    if test -f $$pixmap; then \
	      cp -p $$pixmap $(distdir)/pixmaps; \
	    fi \
	  done \
	fi
	cd $(distdir) ; rm -f $(CLEANFILES)

CLEANFILES = 			\
	gnome-format.desktop		\
	gnome-format.schemas	
