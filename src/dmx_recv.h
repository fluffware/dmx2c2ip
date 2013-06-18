#ifndef __DMX_RECV_H__QDGKDUOV0E__
#define __DMX_RECV_H__QDGKDUOV0E__

#include <glib-object.h>

#define DMX_RECV_TYPE                  (dmx_recv_get_type ())
#define DMX_RECV(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), DMX_RECV_TYPE, DMXRecv))
#define IS_DMX_RECV(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DMX_RECV_TYPE))
#define DMX_RECV_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), DMX_RECV_TYPE, DMXRecvClass))
#define IS_DMX_RECV_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), DMX_RECV_TYPE))
#define DMX_RECV_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), DMX_RECV_TYPE, DMXRecvClass))

typedef struct _DMXRecv DMXRecv;
typedef struct _DMXRecvClass DMXRecvClass;


DMXRecv *
dmx_recv_new(const char *device, GError **err);

gboolean
dmx_recv_channels_changed(DMXRecv *recv, guint from, guint to);

#endif /* __DMX_RECV_H__QDGKDUOV0E__ */
