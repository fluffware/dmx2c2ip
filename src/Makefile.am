
AM_CFLAGS = @GLIB_CFLAGS@

noinst_PROGRAMS = dmx_send
bin_PROGRAMS = dmx2c2ip

dmx2c2ip_SOURCES = dmx_recv.c dmx_recv.h main.c dmx_serial.c dmx_serial.h
dmx2c2ip_LDADD= @GLIB_LIBS@

dmx_send_sources = dmx_send.c