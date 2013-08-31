#ifndef __DMX_RECV_H__QDGKDUOV0E__
#define __DMX_RECV_H__QDGKDUOV0E__

#include <glib-object.h>

#define DMX_RECV_TYPE                  (dmx_recv_get_type ())
#define DMX_RECV(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), DMX_RECV_TYPE, DMXRecv))
#define IS_DMX_RECV(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DMX_RECV_TYPE))
#define DMX_RECV_GET_INTERFACE(obj)        (G_TYPE_INSTANCE_GET_INTERFACE ((obj), DMX_RECV_TYPE, DMXRecvInterface))

typedef struct _DMXRecv DMXRecv;
typedef struct _DMXRecvInterface DMXRecvInterface;

struct _DMXRecvInterface {
   /* Virtual Functions */
  GTypeInterface parent;
  gboolean (*channels_changed) (DMXRecv *recv, guint from, guint to);
  /* Signals */
  /* value = channel_changed & 0xff
     channel = channel_value >> 8;
  */
  void (*channel_changed)(DMXRecv *recv, gint channel_value);
  void (*new_packet)(DMXRecv *recv, guint l, guint8 *data);
};

GType
dmx_recv_get_type (void);

gboolean
dmx_recv_channels_changed(DMXRecv *recv, guint from, guint to);

#endif /* __DMX_RECV_H__QDGKDUOV0E__ */
