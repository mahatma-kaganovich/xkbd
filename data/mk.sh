#!/bin/sh

pkgdatadir=$1
shift
CPPFLAGS=$1
shift

[ "$CPPFLAGS" = "${CPPFLAGS/*-DMINIMAL}" ] && minimal=0 || minimal=1

for i in $*; do
	d=${i%.in}
	sed -e "s,\@pkgdatadir\@,$pkgdatadir," -e "s,\@minimal\@,$minimal," $i >$d
	d1=${d%.conf}
	if [ "$d" != "$d1" ]; then
	    d1=$d1-rg.conf
	    cp -a $d $d1
	    patch -i rg.patch $d1 # || rm $d1
	fi
	true
done
