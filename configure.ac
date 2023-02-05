# Process this file with autoconf to produce a configure script.

AC_INIT([xkbd], [0.8.99])
AM_INIT_AUTOMAKE()
AM_CONFIG_HEADER(config.h)
AC_CONFIG_MACRO_DIRS([m4])

_set_pkg(){
	CPPFLAGS="$CPPFLAGS $2"
	if test x$PKG_CONFIG != xno && $PKG_CONFIG --exists $1; then
		X_LIBS="$X_LIBS $($PKG_CONFIG --libs $1)"
		CFLAGS="$CFLAGS $($PKG_CONFIG --cflags $1)"
	else
		if test x$PKG_CONFIG != xno; then
			AC_MSG_WARN([*** Could not find pkg-config entry for $1 in your path, build may fail***])
		fi
		X_LIBS="$X_LIBS $3"
	fi
}

_set_pkg_xtg(){
	CPPFLAGS="$CPPFLAGS $2"
	if test x$PKG_CONFIG != xno && $PKG_CONFIG --exists $1; then
		X_EXTRA_LIBS="$X_EXTRA_LIBS $($PKG_CONFIG --libs $1)"
		CFLAGS="$CFLAGS $($PKG_CONFIG --cflags $1)"
	else
		if test x$PKG_CONFIG != xno; then
			AC_MSG_WARN([*** Could not find pkg-config entry for $1 in your path, build may fail***])
		fi
		CFLAGS="$CFLAGS $4"
		X_EXTRA_LIBS="$X_EXTRA_LIBS $3"
	fi
}

_get_pkg_var(){
	test x$PKG_CONFIG != xno &&
	    $PKG_CONFIG --exists $1 &&
	    (pkg-config --print-variables x11|grep -q "^$2"'$') &&
		pkg-config --variable=$2 $1
}

AC_PATH_PROG(PKG_CONFIG, pkg-config, no)

dnl check for xft support
AC_ARG_ENABLE(xft,
  [  --disable-xft         disable xft (anti-aliased) support, [default=no]],
  enable_xft=$enableval, enable_xft=yes)

if test x$enable_xft != xno; then
	_set_pkg xft -DUSE_XFT -lXft
fi

AC_ARG_ENABLE(xpm,
  [  --disable-xpm         disable xpm image support, [default=no]],
  enable_xpm=$enableval, enable_xpm=yes)

if test x$enable_xpm != xno; then
	_set_pkg xpm -DUSE_XPM -lXpm
fi

AC_ARG_ENABLE(ss,
  [  --disable-ss         disable XScreenSaver support, [default=no]],
  enable_ss=$enableval, enable_ss=yes)

if test x$enable_ss != xno; then
	_set_pkg xscrnsaver -DUSE_SS -lXss
fi

AC_ARG_ENABLE(xi,
  [  --disable-xi         disable XInput2, [default=no]],
  enable_xi=$enableval, enable_xi=yes)

if test x$enable_xi != xno; then
	_set_pkg xi -DUSE_XI -lXi
fi

AC_ARG_ENABLE(xrandr,
  [  --disable-xrandr         disable xrandr support, [default=no]],
  enable_xrandr=$enableval, enable_xrandr=yes)

if test x$enable_xrandr != xno; then
	_set_pkg xrandr -DUSE_XR -lXrandr
fi

AC_ARG_ENABLE(evdev,
  [  --disable-evdev         disable libevdev support for xtg, [default=no]],
  enable_evdev=$enableval, enable_evdev=yes)

if test x$enable_evdev != xno; then
	_set_pkg_xtg libevdev -DUSE_EVDEV -levdev -I/usr/include/libevdev-1.0
fi

AC_ARG_ENABLE(minimal,
  [  --enable-minimal         minimal build (disable: X sync mode, keycodes, slides), [default=no]],
  enable_minimal=$enableval, enable_minimal=no)

if test x$enable_minimal = xyes; then
      CPPFLAGS="$CPPFLAGS -DMINIMAL"
fi

dnl check for debug build
AC_ARG_ENABLE(debug,
  [  --enable-debug         enable debug build],
  [if test x$enable_debug = xyes; then
      CFLAGS="$CFLAGS -g -DDEBUG"
  fi])

AC_ARG_ENABLE(thread,
  [  --disable-thread         disable thread support for xtg, [default=no]],
  enable_thread=$enableval, enable_thread=yes)



# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

# Checks for libraries.

# Checks for header files.
AC_PATH_X
AC_PATH_XTRA
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h string.h sys/param.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST

# Checks for library functions.
AC_FUNC_MALLOC
AC_TYPE_SIGNAL
AC_CHECK_FUNCS([strchr strdup])

if test x$enable_thread != xno && test x$enable_minimal != xyes; then
    AC_MSG_CHECKING([for simple X11/Xthreads.h with modern glibc])
    AC_LINK_IFELSE(
        [AC_LANG_SOURCE([`echo '#include <X11/Xthreads.h>
        int main(){ xthread_fork(NULL,NULL); }'`])],
        [have_simple_Xthreads=yes],
        [have_simple_Xthreads=no]
    )
    AC_MSG_RESULT($have_simple_Xthreads)
    if test x$have_simple_Xthreads != xno; then
        CPPFLAGS="$CPPFLAGS -DUSE_XTHREAD"
    else
        AC_CHECK_HEADERS([X11/Xthreads.h])
        if test "x$ac_cv_header_X11_Xthreads_h" = xyes; then
          CPPFLAGS="$CPPFLAGS -DUSE_XTHREAD"
        fi
        AC_MSG_CHECKING([for x11 xthreadlib])
        if __xthlib=$(_get_pkg_var x11 xthreadlib); then
            X_EXTRA_LIBS="$X_EXTRA_LIBS $__xthlib"
        else
            __xthlib=no
        fi
        AC_MSG_RESULT($__xthlib)
        if test x"$__xthlib" = x-lpthread || test x"$__xthlib" = xno; then
            AX_PTHREAD([have_pthread=yes], [have_pthread=no])
            if x"$have_pthread" = xyes; then
                if test x"$__xthlib" != x"$PTHREAD_LIBS"; then
                    X_EXTRA_LIBS="$X_EXTRA_LIBS $PTHREAD_LIBS"
                fi
                CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
                CPPFLAGS="$CPPFLAGS -DUSE_PTHREAD"
            fi
        fi
    fi
fi

AC_OUTPUT(Makefile 
          src/Makefile
          data/Makefile
          doc/Makefile 
          data/img/Makefile
          packaging/Makefile
          packaging/ipkg/Makefile
          packaging/ipkg/control )
