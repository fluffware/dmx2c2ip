#ifndef __SERIAL_DMX_RECV_H__8RXDG1BHYC__
#define __SERIAL_DMX_RECV_H__8RXDG1BHYC__

#include <glib-object.h>
#include <dmx_recv.h>

#define SERIAL_DMX_RECV_TYPE                  (serial_dmx_recv_get_type ())
#define SERIAL_DMX_RECV(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), SERIAL_DMX_RECV_TYPE, SerialDMXRecv))
#define IS_SERIAL_DMX_RECV(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SERIAL_DMX_RECV_TYPE))
#define SERIAL_DMX_RECV_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), SERIAL_DMX_RECV_TYPE, SerialDMXRecvClass))
#define IS_SERIAL_DMX_RECV_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), SERIAL_DMX_RECV_TYPE))
#define SERIAL_DMX_RECV_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), SERIAL_DMX_RECV_TYPE, SerialDMXRecvClass))

typedef struct _SerialDMXRecv SerialDMXRecv;
typedef struct _SerialDMXRecvClass SerialDMXRecvClass;


DMXRecv *
serial_dmx_recv_new(const char *device, GError **err);

#endif /* __SERIAL_DMX_RECV_H__8RXDG1BHYC__ */
