#ifndef __BUFFERD_DMX_RECV_PRIVATE_H__7F90RFGJ4V__
#define __BUFFERD_DMX_RECV_PRIVATE_H__7F90RFGJ4V__

#include "buffered_dmx_recv.h"

#define MAX_BUFFER 512

struct _BufferedDMXRecv
{
  GObject parent_instance;
  guint8 *user_buffer;
  gsize user_length;
  guint8 *queue_buffer;
  gsize queue_length;
  
  /* A bit array indicating if a channel has changed since last signal
     emission */
#define MAX_CHANGE_BIT_WORDS ((MAX_BUFFER+sizeof(guint32)-1) / sizeof(guint32))
  guint32 change_bits[MAX_CHANGE_BIT_WORDS];

  /* This should be held when accessing write_buffer 
     or when swapping buffers. */
  GMutex buffer_mutex;
  
  /* The pending idle source */
  GSource *idle_source;
  
  /* Context of thread creating this object, used for emitting signals */
  GMainContext *creator_main_context;
  
};

struct _BufferedDMXRecvClass
{
  GObjectClass parent_class;

  /* class members */
};

void
buffered_dmx_recv_queue(BufferedDMXRecv *recv,
			const guint8 *data, gsize length);

#endif /* __BUFFERD_DMX_RECV_PRIVATE_H__7F90RFGJ4V__ */
