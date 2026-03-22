#!/bin/sh

[ -z "$*" ] && set -- /usr/local/share/xkbd -- *.in

pkgdatadir=$1
shift
CPPFLAGS=$1
shift

[ "$CPPFLAGS" = "${CPPFLAGS/*-DMINIMAL}" ] && minimal=0 || minimal=1

for i in $*; do
	d=${i%.in}
	[ "$d" = $i ] && continue
	[ "${pkgdatadir%%/*}" = clean ] && {
		[ -e "$d" ] && unlink "$d"
		continue
	}
	sed -e "s,\@pkgdatadir\@,$pkgdatadir," -e "s,\@minimal\@,$minimal," $i >$d
	true
done
