SUBDIRS = img
kbdconfigdir = $(pkgdatadir)
kbdconfig_DATA = *.???? xkbd-onoff xkbd-config

CLEANFILES = $(kbdconfig_DATA)

EXTRA_DIST = *.in

*.????: *.????.in
	sh ./mk.sh $(pkgdatadir) *.????.in

xkbd-config: xkbd-config.in
	sh ./mk.sh $(pkgdatadir) xkbd-config.in

xkbd-onoff: xkbd-onoff.in
	sh ./mk.sh $(pkgdatadir) xkbd-onoff.in
