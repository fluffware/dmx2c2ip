#include "serial_dmx_recv.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dmx_serial.h>
#include <stdint.h>
#include <stdio.h>
#include <buffered_dmx_recv_private.h>

#define STATE_NORMAL (1<<0)
#define STATE_ESCAPE (1<<1)
#define STATE_FRAMING (1<<2)
#define STATE_SKIP (1<<3)
#define STATE_FIRST (1<<4)

#define MAX_BUFFER 512

struct _SerialDMXRecv
{
  BufferedDMXRecv parent_instance;
  int serial_fd;
  gint source;
  guint state;
  guint8 read_buffer[MAX_BUFFER];
  gsize read_length;
  GThread *read_thread;
  /* Created by and destroyed by the read thread */
  GMainLoop *read_main_loop;
  GMainContext *read_main_context;
  GCond read_start_cond;
  GMutex read_mutex;
  GRWLock read_buffer_lock;
  guint frame_count;
};

struct _SerialDMXRecvClass
{
  BufferedDMXRecvClass parent_class;

  /* class members */

};

G_DEFINE_TYPE (SerialDMXRecv, serial_dmx_recv, BUFFERED_DMX_RECV_TYPE)
	


static gpointer read_thread(gpointer data);

static void
stop_read_thread(SerialDMXRecv *recv)
{
  if (recv->read_thread) {
    g_mutex_lock (&recv->read_mutex);
    if (recv->read_main_loop)
      g_main_loop_quit(recv->read_main_loop);
    g_mutex_unlock (&recv->read_mutex);
    g_thread_join(recv->read_thread);
    recv->read_thread = NULL;
    g_mutex_clear(&recv->read_mutex);
    g_rw_lock_clear (&recv->read_buffer_lock);
  }
}

static void
start_read_thread(SerialDMXRecv *recv)
{
  if (!recv->read_thread) {
    g_cond_init(&recv->read_start_cond);
    g_mutex_init(&recv->read_mutex);
    g_rw_lock_init(&recv->read_buffer_lock);
    recv->read_thread = g_thread_new("DMX read", read_thread, recv);
    g_mutex_lock (&recv->read_mutex);
    while(!recv->read_main_loop) {
      g_cond_wait(&recv->read_start_cond, &recv->read_mutex);
    }
    g_mutex_unlock (&recv->read_mutex);
    g_cond_clear(&recv->read_start_cond);
  }
  
}

static void
dispose(GObject *gobj)
{
  SerialDMXRecv *recv = SERIAL_DMX_RECV(gobj);
  stop_read_thread(recv);
  if (recv->serial_fd >= 0) {
    close(recv->serial_fd);
    recv->serial_fd = -1;
  }
 
  G_OBJECT_CLASS(serial_dmx_recv_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  G_OBJECT_CLASS(serial_dmx_recv_parent_class)->finalize(gobj);
}

static void
serial_dmx_recv_class_init (SerialDMXRecvClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
}

static void
serial_dmx_recv_init (SerialDMXRecv *self)
{
  self->serial_fd = -1;
  self->source = 0;
  self->state = STATE_NORMAL;
  self->read_main_loop = NULL;
  self->read_main_context = NULL;
  self->frame_count = 0;
  self->read_length = 0;
}


/* Decode received data and put it in the right buffer. Switch buffer on break.
 */
static void
handle_data(SerialDMXRecv *recv, const uint8_t *data, gsize data_length)	    
{
  guint state = recv->state;
  while(data_length > 0) {
    uint8_t c = *data++;
    data_length--;
    if (state & STATE_FIRST) {
      if (c != 0) state |= STATE_SKIP;
      state &= ~STATE_FIRST;
    } else if (state & STATE_FRAMING) {
      if (c == 0x00) {
	
	if (recv->read_length > 0) {
	  buffered_dmx_recv_queue(&recv->parent_instance,
				  recv->read_buffer, recv->read_length);
	  recv->frame_count++;
	  recv->read_length = 0;
	}
	state &= ~STATE_SKIP;
	state |= STATE_FIRST;
      }
      state &= ~(STATE_ESCAPE | STATE_FRAMING);
    } else if ((state & STATE_ESCAPE) && c == 0x00) {
      state |= STATE_FRAMING;
    } else if (!(state & STATE_ESCAPE)  && c == 0xff) {
      state |= STATE_ESCAPE;
    } else if (state & STATE_SKIP) {
      /* Do nothing */
    } else {
      state &= ~(STATE_ESCAPE | STATE_FRAMING|STATE_FIRST);
      if (recv->read_length < MAX_BUFFER) {
	recv->read_buffer[recv->read_length++] = c;
      } else {
	state |= STATE_SKIP;
      }
    }
  }
  /* signal_changes(recv, from); */
  recv->state = state;
}

struct DMXSource
{
  GSource source;
  GPollFD pollfd;
  SerialDMXRecv *recv;
};


static gboolean
prepare(GSource *source, gint *timeout_)
{
  *timeout_ = -1;
  return FALSE;
}

static gboolean check(GSource *source)
{
  gushort revents = ((struct DMXSource*)source)->pollfd.revents;
  if (revents & G_IO_IN) return TRUE;
  return FALSE;
}

static gboolean
dispatch(GSource *source, GSourceFunc callback, gpointer user_data)
{
  uint8_t buffer[16];
  struct DMXSource *dmx_source = (struct DMXSource*)source;
  ssize_t r = read(dmx_source->pollfd.fd, buffer, sizeof(buffer));
  /* g_debug("Dispatch"); */
  handle_data(dmx_source->recv, buffer, r);
  
  return TRUE;
}

static GSourceFuncs source_funcs =
  {
    prepare,
    check,
    dispatch,
    NULL
  };

static gpointer
read_thread(gpointer data)
{
  struct DMXSource *source;
  SerialDMXRecv *recv = data;
  g_mutex_lock(&recv->read_mutex);
  recv->read_main_context = g_main_context_new();
  recv->read_main_loop = g_main_loop_new( recv->read_main_context, TRUE);
  g_mutex_unlock(&recv->read_mutex);
  g_cond_signal(&recv->read_start_cond);
   source =
    (struct DMXSource*)g_source_new(&source_funcs, sizeof(struct DMXSource));
  g_assert(source != NULL);
  source->pollfd.events = G_IO_IN | G_IO_ERR;
  source->pollfd.fd = recv->serial_fd;
  source->recv = recv;
  g_source_add_poll(&source->source, &((struct DMXSource*)source)->pollfd);
  recv->source = g_source_attach(&source->source,  recv->read_main_context);
  g_debug("Read thread started");
  g_main_loop_run(recv->read_main_loop);
  g_debug("Read thread stopped");

  g_source_remove(recv->source);
  
  g_mutex_lock(&recv->read_mutex);
  g_main_loop_unref(recv->read_main_loop);
  recv->read_main_loop = NULL;
  g_main_context_unref(recv->read_main_context);
  recv->read_main_context = NULL;
  g_mutex_unlock(&recv->read_mutex);
  return NULL; 
}

DMXRecv *
serial_dmx_recv_new(const char *device, GError **err)
{
  SerialDMXRecv *recv = g_object_new (SERIAL_DMX_RECV_TYPE, NULL);
  recv->serial_fd = dmx_serial_open(device, err);
  if (recv->serial_fd < 0) {
    g_object_unref(recv);
    return NULL;
  }
  start_read_thread(recv);
 
  return DMX_RECV(recv);
}
