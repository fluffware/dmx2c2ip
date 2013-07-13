
AM_CFLAGS = @GLIB_CFLAGS@ @MICROHTTPD_CFLAGS@ @JSONGLIB_CFLAGS@

noinst_PROGRAMS = dmx_send
bin_PROGRAMS = dmx2c2ip c2ip_tool

dmx2c2ip_SOURCES = main.c \
dmx_recv.c dmx_recv.h \
dmx_serial.c dmx_serial.h \
httpd.c httpd.h \
c2ip_scan.c c2ip_scan.h c2ip_scan_marshal.c c2ip_scan_marshal.h \
c2ip_connection_manager.c c2ip_connection_manager.h \
c2ip_connection_manager_marshal.c c2ip_connection_manager_marshal.h \
c2ip_connection.c c2ip_connection.h \
c2ip_connection_values.c c2ip_connection_values.h \
c2ip_connection_values_marshal.c c2ip_connection_values_marshal.h \
c2ip_value.c c2ip_value.h \
c2ip_device.c c2ip_device.h \
c2ip_strings.c c2ip_strings.h

dmx2c2ip_LDADD= @GLIB_LIBS@ @MICROHTTPD_LIBS@ @JSONGLIB_LIBS@ -lm

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

BUILT_SOURCES =  c2ip_scan_marshal.c c2ip_scan_marshal.h \
c2ip_connection_values_marshal.c c2ip_connection_values_marshal.h \
c2ip_connection_manager_marshal.c c2ip_connection_manager_marshal.h