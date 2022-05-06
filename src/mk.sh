#!/bin/bash
# quick test

: ${LDFLAGS:="-Wl,-O1 -Wl,--as-needed"}
: ${CFLAGS:="-O2 -pipe -Wmaybe-uninitialized"}

CFLAGS+=" -fwhole-program"
LDLAGS+=" -Wl,--strip-all"

e(){
	echo "${@//\"/\\\"}"
	"${@}"
}

_c(){
	local i skip=false c=/dev/null w=
	[ -e $1 ] && skip=true
	$skip && for i in $2; do
		[ $i -nt $1 ] && skip=false && echo "# changed: $i"
	done
	$skip && return 0
	[[ "$2" != *\ * ]] && w=-fwhole-program
	echo "# compiling $1"
	if [[ " $CFLAGS " == *' -fwhole-program '* ]] && [[ "$2" == *\ * ]]; then
		c="$2"
		set -- "$1" - "$3" "$4"
	fi
	set -- ${CC:-cc} -o $1 -x c $2 \
		$CFLAGS $LDFLAGS $CPPFLAGS \
		$(pkg-config --cflags --libs x11 $3) ${4//\"/\"}
	if [ "$c" = /dev/null ]; then
		e "${@}"
	else
		echo "cat $c|${@//\"/\\\"}"
		cat $c|"${@}"
	fi &
}

[ /usr/include/X11/keysymdef.h -nt ks2unicode.c ] && e ./ks2unicode.pl
_c xkbd "box.c button.c kb.c ks2unicode.c xkbd.c" "xtst xi xrandr xft xpm" "-DVERSION=\"1.8.999\" -DDEFAULTCONFIG=\"/etc/xkbd.conf\" -DUSE_XFT -DUSE_XPM -DUSE_SS -DUSE_XI -DUSE_XR"
_c xssevent xssevent.c xscrnsaver
_c xkbswitch xkbswitch.c
_c xkbdd xtg.c "" -DNO_ALL
_c xkbdd-xss xtg.c "xext xscrnsaver" "-DNO_ALL -DXSS"
_c xtg xtg.c "xtst xi xrandr xext xscrnsaver xfixes" "-DUSE_XTHREAD $([ -e /usr/include/libevdev-1.0 ] && echo -DUSE_EVDEV -levdev -I/usr/include/libevdev-1.0)"

wait
