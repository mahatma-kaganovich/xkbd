DEFAULT_CONFIG=$(pkgdatadir)/all.qwerty.xkbd

LDADD = @X_LIBS@ -lX11 -lXtst $(XPM_LIBS) $(XFT_LIBS)
AM_CFLAGS = -O2 -DDEFAULTCONFIG=\"$(DEFAULT_CONFIG)\" -Dprefix=\"${prefix}\"

bin_PROGRAMS = xkbd
xkbd_SOURCES = xkbd.c libXkbd.c box.c button.c kb.c libvirtkeys.c ks2unicode.c \
                      structs.h box.h  button.h  kb.h  libvirtkeys.h ks2unicode.h \
                      libXkbd.h
xkbd_LDADD   = $(LDADD)

CLEANFILES = ks2unicode.c
ks2unicode.c:
	perl ks2unicode.pl
