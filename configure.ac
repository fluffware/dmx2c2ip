AC_INIT([dmx2c2ip], [0.1])
AC_CONFIG_SRCDIR([src/main.c])
AM_INIT_AUTOMAKE
AC_CONFIG_HEADERS([config.h])
AC_PROG_CC
AC_PROG_CXX
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

AC_ARG_WITH([ola],[AS_HELP_STRING([--with-ola], [OLA support])],
            [],
	    [with_ola=check])
AS_IF([test x$with_ola != xno],
[
# Check for OLA
AC_MSG_CHECKING([for OLA library]) 
if pkg-config libola --exists; then
OLA_CXXFLAGS=`pkg-config libola --cflags`
AC_SUBST(OLA_CXXFLAGS)
OLA_LIBS=`pkg-config libola --libs`
AC_SUBST(OLA_LIBS)
OLA_VERSION=`pkg-config libola --modversion`;
AC_MSG_RESULT(${OLA_VERSION});
AC_DEFINE([WITH_OLA],[1],[Define if you have OLA libraries])
with_ola=yes
else
AC_MSG_RESULT([not found])
with_ola=no
fi
])
AM_CONDITIONAL([WITH_OLA], [test x$with_ola = xyes])

AC_CONFIG_FILES([Makefile src/Makefile src/dmx2c2ip.conf src/http_root/Makefile])
AC_OUTPUT
