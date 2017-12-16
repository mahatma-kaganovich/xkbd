DEFAULT_CONFIG=$(pkgdatadir)/all.qwerty.xkbd

LDADD = @X_LIBS@ -lX11 -lXtst $(XPM_LIBS) $(XFT_LIBS)
CFLAGS = @CFLAGS@ -DDEFAULTCONFIG=\"$(DEFAULT_CONFIG)\"

bin_PROGRAMS = xkbd
xkbd_SOURCES = xkbd.c libXkbd.c box.c button.c kb.c libvirtkeys.c \
                      structs.h box.h  button.h  kb.h  libvirtkeys.h \
                      libXkbd.h
xkbd_LDADD   = $(LDADD)
        

