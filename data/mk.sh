#!/bin/sh

pkgdatadir=$1
shift
CPPFLAGS=$1
shift

[ "$CPPFLAGS" = "${CPPFLAGS/*-DMINIMAL}" ] && minimal=0 || minimal=1

for i in $*; do
	sed -e "s,\@pkgdatadir\@,$pkgdatadir," -e "s,\@minimal\@,$minimal," $i >${i%.in}
done
