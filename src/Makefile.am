SUBDIRS=http_root

AM_CFLAGS = @GLIB_CFLAGS@ @MICROHTTPD_CFLAGS@ @JSONGLIB_CFLAGS@ -Wno-long-long -I $(srcdir)/httpd
AM_CXXFLAGS = @OLA_CXXFLAGS@ @GLIB_CFLAGS@ -Wno-long-long

noinst_PROGRAMS = dmx_send c2ip_tool
bin_PROGRAMS = dmx2c2ip

dmx2c2ip_SOURCES = main.c \
dmx_recv.c dmx_recv.h \
buffered_dmx_recv.c buffered_dmx_recv.h buffered_dmx_recv_private.h \
serial_dmx_recv.c serial_dmx_recv.h \
dmx_serial.c dmx_serial.h \
httpd/httpd.c httpd/httpd.h \
c2ip_scan.c c2ip_scan.h c2ip_scan_marshal.c c2ip_scan_marshal.h \
c2ip_connection_manager.c c2ip_connection_manager.h \
c2ip_connection_manager_marshal.c c2ip_connection_manager_marshal.h \
c2ip_connection.c c2ip_connection.h \
c2ip_connection_values.c c2ip_connection_values.h \
c2ip_connection_values_marshal.c c2ip_connection_values_marshal.h \
c2ip_function.c c2ip_function.h \
c2ip_device.c c2ip_device.h \
c2ip_strings.c c2ip_strings.h \
sqlite/sqlite3.c sqlite/sqlite3.h \
dmx_c2ip_mapper.c dmx_c2ip_mapper.h \
dmx_c2ip_mapper_marshal.c dmx_c2ip_mapper_marshal.h

if WITH_OLA 
dmx2c2ip_SOURCES += ola_dmx_recv.c ola_dmx_recv.h ola_wrapper.cpp ola_wrapper.h 
endif


dmx2c2ip_LDADD= @GLIB_LIBS@ @MICROHTTPD_LIBS@ @JSONGLIB_LIBS@ @OLA_LIBS@ -lm -ldl

dmx_send_sources = dmx_send.c

c2ip_tool_SOURCES = c2ip_tool.c \
c2ip_scan.c c2ip_scan.h c2ip_scan_marshal.c c2ip_scan_marshal.h \
c2ip_connection_manager.c c2ip_connection_manager.h \
c2ip_connection_manager_marshal.c c2ip_connection_manager_marshal.h \
c2ip_connection.c c2ip_connection.h \
c2ip_decode.c c2ip_decode.h \
c2ip_strings.c c2ip_strings.h

c2ip_tool_LDADD= @GLIB_LIBS@ -lreadline

%marshal.h: %marshal.list
	glib-genmarshal --header --prefix="$*marshal" $< > $@.tmp && \
        mv $@.tmp $@

%marshal.c: %marshal.list
	glib-genmarshal --body --prefix="$*marshal" $< > $@.tmp && \
        mv $@.tmp $@

EXTRA_DIST=dmx2c2ip.conf.in
sysconf_DATA=dmx2c2ip.conf
BUILT_SOURCES =  c2ip_scan_marshal.c c2ip_scan_marshal.h \
c2ip_connection_values_marshal.c c2ip_connection_values_marshal.h \
c2ip_connection_manager_marshal.c c2ip_connection_manager_marshal.h \
dmx_c2ip_mapper_marshal.c dmx_c2ip_mapper_marshal.h
