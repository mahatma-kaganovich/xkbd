#EXTRA_DIST = autoconfigure.sh
SUBDIRS = src data packaging doc
EXTRA_DIST = xkbd.desktop xkbd-onoff.desktop xkbd.png
ACLOCAL_AMFLAGS = -I m4

desktopdir = $(datadir)/applications/inputmethods
desktop_DATA = xkbd.desktop xkbd-onoff.desktop

pixmapsdir = $(datadir)/pixmaps/
pixmaps_DATA = xkbd.png

ipkg: Makefile
	$(MAKE) -C packaging/ipkg ipkg
