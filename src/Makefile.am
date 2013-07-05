
AM_CFLAGS = @GLIB_CFLAGS@ @MICROHTTPD_CFLAGS@ @JSONGLIB_CFLAGS@

noinst_PROGRAMS = dmx_send
bin_PROGRAMS = dmx2c2ip

dmx2c2ip_SOURCES = main.c \
dmx_recv.c dmx_recv.h \
dmx_serial.c dmx_serial.h \
httpd.c httpd.h \
c2ip_scan.c c2ip_scan.h c2ip_scan_marshal.c c2ip_scan_marshal.h \
c2ip_connection_manager.c c2ip_connection_manager.h \
c2ip_connection.c c2ip_connection.h

dmx2c2ip_LDADD= @GLIB_LIBS@ @MICROHTTPD_LIBS@ @JSONGLIB_LIBS@

dmx_send_sources = dmx_send.c

%marshal.h: %marshal.list
	glib-genmarshal --header --prefix="$*marshal" $< > $@.tmp && \
        mv $@.tmp $@

%marshal.c: %marshal.list
	glib-genmarshal --body --prefix="$*marshal" $< > $@.tmp && \
        mv $@.tmp $@

BUILT_SOURCES =  c2ip_scan_marshal.c c2ip_scan_marshal.h
