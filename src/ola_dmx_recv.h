#ifndef __OLA_DMX_RECV_H__II6RAG6AAZ__
#define __OLA_DMX_RECV_H__II6RAG6AAZ__

#include <glib-object.h>
#include <dmx_recv.h>

#define OLA_DMX_RECV_TYPE                  (ola_dmx_recv_get_type ())
#define OLA_DMX_RECV(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), OLA_DMX_RECV_TYPE, OLADMXRecv))
#define IS_OLA_DMX_RECV(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), OLA_DMX_RECV_TYPE))
#define OLA_DMX_RECV_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), OLA_DMX_RECV_TYPE, OLADMXRecvClass))
#define IS_OLA_DMX_RECV_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), OLA_DMX_RECV_TYPE))
#define OLA_DMX_RECV_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), OLA_DMX_RECV_TYPE, OLADMXRecvClass))

typedef struct _OLADMXRecv OLADMXRecv;
typedef struct _OLADMXRecvClass OLADMXRecvClass;


DMXRecv *
ola_dmx_recv_new(guint universe, GError **err);

#endif /* __OLA_DMX_RECV_H__II6RAG6AAZ__ */
