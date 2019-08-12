DEFAULT_CONFIG=$(pkgdatadir)/xkbd-std.conf

LDADD = @X_LIBS@ -lX11 -lXtst
AM_CFLAGS = -O2 -DDEFAULTCONFIG=\"$(DEFAULT_CONFIG)\" -Dprefix=\"${prefix}\" -Wmaybe-uninitialized

bin_PROGRAMS = xkbd xssevent
xkbd_SOURCES = xkbd.c box.c button.c kb.c ks2unicode.c \
                      structs.h box.h  button.h  kb.h  ks2unicode.h
xkbd_LDADD   = $(LDADD)
xssevent_LDADD = -lX11 -lXss

CLEANFILES = ks2unicode.c
ks2unicode.c:
	perl ks2unicode.pl
