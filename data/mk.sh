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
	d1=${d%.conf}
	if [ "$d" != "$d1" ]; then
		for j in *.patch; do
			j=${j##*/}
			j=${j%.patch}
			d2=$d1-$j.conf
			cp -a $d $d2
			patch --no-backup-if-mismatch -r - -sfi $j.patch $d2
		done
	fi
	true
done
