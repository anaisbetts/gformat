## Process this file with automake to produce Makefile.in.
##
gfloppy_helpdir = $(datadir)/gnome/help/gfloppy/az

SGML_FILES = \
	gfloppy.sgml

HTML_FILES = authors.html	license.html	usage.html	bugs.html \
	index.html	ln7.html	troubleshooting.html


EXTRA_DIST = \
	topic.dat $(SGML_FILES)

gfloppy_help_DATA = ${HTML_FILES} topic.dat

#
# This is icky but insures that the doc files are generated when the sgml 
# file changes, plus it only generates em once. The distclean target 
# will take care of clobbering the generated html and the directory that
# db2html makes, while the dist-hook will make sure that the html files 
# and directory gets copied into a dist (so mere mortals w/o docbook can 
# build this) The install-data-local target copies all the bits and pieces
# where the runtime help system can find it.
#
#
# The layout this expects things to be in...
# ./               all *.html files
# ./<doc name>/    output of db2html winds up here
# ./*.png          any images/figures needed for the documentation.
${HTML_FILES} : $(SGML_FILES)
	if [ ! -d gfloppy ] ; then cd $(srcdir) && db2html gfloppy.sgml ; fi
	-cd $(srcdir) && cp gfloppy/$@ .
	if [ -d gfloppy.junk ] ; then cd $(srcdir) && rm -rf gfloppy.junk ; fi

dist-hook:
	$(mkinstalldirs) $(distdir)/gfloppy 
	$(mkinstalldirs) $(distdir)/gfloppy/stylesheet-images
	-cp $(srcdir)/gfloppy/*.html gfloppy/*.css $(distdir)/gfloppy
	-cp $(srcdir)/gfloppy/stylesheet-images/*.gif \
	 $(distdir)/gfloppy/stylesheet-images
	-cp $(srcdir)/*.html $(distdir)
	-cp $(srcdir)/*.png $(distdir)

distclean-generic:
	-rm -f Makefile
	-rm -f config.cache config.log stamp-h stamp-h[0-9]*
	-rm -f $(HTML_FILES)
	-rm -rf gfloppy

install-data-local: index.html
	$(mkinstalldirs) $(DESTDIR)$(gfloppy_helpdir)/stylesheet-images
	-for file in $(srcdir)/gfloppy/*.html $(srcdir)/gfloppy/*.css; do \
	  basefile=`basename $$file`; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(gfloppy_helpdir)/$$basefile; \
	done
	-for file in $(srcdir)/*.png ; do \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(gfloppy_helpdir); \
	done
	-for file in $(srcdir)/*.sgml ; do \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(gfloppy_helpdir); \
	done

	-for file in $(srcdir)/gfloppy/stylesheet-images/*.gif; do \
	  basefile=`basename $$file`; \
	  $(INSTALL_DATA) $$file $(DESTDIR)$(gfloppy_helpdir)/stylesheet-images/$$basefile; \
	done





