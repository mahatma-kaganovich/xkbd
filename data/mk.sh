#!/bin/sh

pkgdatadir=$1
shift

for i in $*; do
	sed -e "s,\@pkgdatadir\@,$pkgdatadir," $i >${i%.in}
done
