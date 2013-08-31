#ifndef __BUFFERD_DMX_RECV_H__4ZJ38VQB6K__
#define __BUFFERD_DMX_RECV_H__4ZJ38VQB6K__


#include <glib-object.h>
#include <dmx_recv.h>

#define BUFFERED_DMX_RECV_TYPE                  (buffered_dmx_recv_get_type ())
#define BUFFERED_DMX_RECV(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), BUFFERED_DMX_RECV_TYPE, BufferedDMXRecv))
#define IS_BUFFERED_DMX_RECV(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), BUFFERED_DMX_RECV_TYPE))
#define BUFFERED_DMX_RECV_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), BUFFERED_DMX_RECV_TYPE, BufferedDMXRecvClass))
#define IS_BUFFERED_DMX_RECV_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), BUFFERED_DMX_RECV_TYPE))
#define BUFFERED_DMX_RECV_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), BUFFERED_DMX_RECV_TYPE, BufferedDMXRecvClass))

typedef struct _BufferedDMXRecv BufferedDMXRecv;
typedef struct _BufferedDMXRecvClass BufferedDMXRecvClass;

GType buffered_dmx_recv_get_type (void);

#endif /* __BUFFERD_DMX_RECV_H__4ZJ38VQB6K__ */
