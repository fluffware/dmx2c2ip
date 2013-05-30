
AM_CFLAGS = @GLIB_CFLAGS@ @MICROHTTPD_CFLAGS@

noinst_PROGRAMS = dmx_send
bin_PROGRAMS = dmx2c2ip

dmx2c2ip_SOURCES = main.c \
dmx_recv.c dmx_recv.h \
dmx_serial.c dmx_serial.h \
httpd.c httpd.h
dmx2c2ip_LDADD= @GLIB_LIBS@ @MICROHTTPD_LIBS@

dmx_send_sources = dmx_send.c