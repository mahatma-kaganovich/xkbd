SUBDIRS = img
kbdconfigdir = $(pkgdatadir)
kbdconfig_DATA = *.????
bin_PROGRAMS = xkbd-onoff xkbd-config
CLEANFILES =  $(kbdconfig_DATA) $(bin_PROGRAMS)

EXTRA_DIST = *.in

*.????: *.????.in
	sh ./mk.sh $(pkgdatadir) *.????.in

xkbd-config$(EXEEXT): xkbd-config.in
	sh ./mk.sh $(pkgdatadir) xkbd-config.in

xkbd-onoff$(EXEEXT): xkbd-onoff.in
	sh ./mk.sh $(pkgdatadir) xkbd-onoff.in
