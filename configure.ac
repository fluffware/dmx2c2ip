AC_INIT([dmx2c2ip], [0.1])
AC_CONFIG_SRCDIR([src/main.c])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])
AC_PROG_CC
AC_SYS_LARGEFILE
AM_PATH_GLIB_2_0(2.30.0,,, [gobject, gio])
# Check for microhttpd
MICROHTTPD_CFLAGS=""
AC_CHECK_HEADERS([microhttpd.h],[MICROHTTPD_CFLAGS=""])
MICROHTTPD_LIBS=""
AC_CHECK_LIB([microhttpd],[MHD_start_daemon],[MICROHTTPD_LIBS="-lmicrohttpd"])
AC_SUBST(MICROHTTPD_CFLAGS)
AC_SUBST(MICROHTTPD_LIBS,$MICROHTTPD_LIBS)

# Check for JSON-GLib
AC_MSG_CHECKING([for JSON-GLib]) 
if pkg-config json-glib-1.0 --exists; then
JSONGLIB_CFLAGS=`pkg-config json-glib-1.0 --cflags`
AC_SUBST(JSONGLIB_CFLAGS)
JSONGLIB_LIBS=`pkg-config json-glib-1.0 --libs`
AC_SUBST(JSONGLIB_LIBS)
JSONGLIB_VERSION=`pkg-config json-glib-1.0 --modversion`;
AC_MSG_RESULT(${JSONGLIB_VERSION});
else
AC_MSG_RESULT([not found])
fi

AC_CONFIG_FILES([Makefile src/Makefile ])
AC_OUTPUT
