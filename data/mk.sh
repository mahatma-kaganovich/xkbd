#!/bin/sh

echo ==== $*

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
		for j in *.patch; do
			j=${j##*/}
			j=${j%.patch}
			d1=$d1-$j.conf
			cp -a $d $d1
			echo "patch -i $j.patch $d1"
			patch -i $j.patch $d1 || rm "$d1" "$d1".orig "$d1".rej -f
		done
	fi
	true
done
