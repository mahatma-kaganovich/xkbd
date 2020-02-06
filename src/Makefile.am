DEFAULT_CONFIG=$(pkgdatadir)/xkbd-std.conf

LDADD = @X_LIBS@ -lX11 -lXtst
AM_CFLAGS = -O2 -DDEFAULTCONFIG=\"$(DEFAULT_CONFIG)\" -Dprefix=\"${prefix}\" -Wmaybe-uninitialized

bin_PROGRAMS = xkbd xssevent xkbdd xkbdd-xss xkbswitch
xkbd_SOURCES = xkbd.c box.c button.c kb.c ks2unicode.c \
                      structs.h box.h  button.h  kb.h  ks2unicode.h
xkbdd_xss_SOURCES = xkbdd.c
xkbdd_xss_CPPFLAGS = -DXSS
xkbd_LDADD   = $(LDADD)
xssevent_LDADD = -lX11 -lXss
xkbdd_LDADD = -lX11
xkbdd_xss_LDADD = -lX11 -lXss
xkbswitch_LDADD = -lX11

CLEANFILES = ks2unicode.c
ks2unicode.c:
	perl ks2unicode.pl
