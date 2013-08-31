#ifndef __BUFFERD_DMX_RECV_PRIVATE_H__7F90RFGJ4V__
#define __BUFFERD_DMX_RECV_PRIVATE_H__7F90RFGJ4V__

#include "buffered_dmx_recv.h"

#define MAX_BUFFER 512

struct _BufferedDMXRecv
{
  GObject parent_instance;
  guint8 *read_buffer;
  gsize read_length;
  guint8 *write_buffer;
  gsize write_length;
  
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

guint8*
buffered_dmx_recv_start_write(BufferedDMXRecv *recv);

void
buffered_dmx_recv_end_write(BufferedDMXRecv *recv, gsize len);

#endif /* __BUFFERD_DMX_RECV_PRIVATE_H__7F90RFGJ4V__ */
