#include "serial_dmx_recv.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dmx_serial.h>
#include <stdint.h>
#include <stdio.h>

#define STATE_NORMAL (1<<0)
#define STATE_ESCAPE (1<<1)
#define STATE_FRAMING (1<<2)
#define STATE_SKIP (1<<3)
#define STATE_FIRST (1<<4)

#define MAX_BUFFER 512

struct _SerialDMXRecv
{
  GObject parent_instance;
  int serial_fd;
  gint source;
  guint state;
  guint8 buffers[2][MAX_BUFFER];
  /* Which of above buffers is currently being used for receiving. The
     other one is available for reading by the user from the
     new-packet signal handler. */
  guint recv_buffer;
  guint buffer_length[2];
  /* A bit array indicating if a channel has changed since last signal
     emission */
#define MAX_CHANGE_BIT_WORDS ((MAX_BUFFER+sizeof(guint32)-1) / sizeof(guint32))
  guint32 change_bits[MAX_CHANGE_BIT_WORDS];
  /* Context of thread creating this object, used for emitting signals */
  GMainContext *creator_main_context;
  
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
  GObjectClass parent_class;

  /* class members */

};

static gboolean
serial_dmx_recv_channels_changed(DMXRecv *recv, guint from, guint to);

static void
dmx_recv_init(DMXRecvInterface *iface) {
  iface->channels_changed = serial_dmx_recv_channels_changed;
}

G_DEFINE_TYPE_WITH_CODE (SerialDMXRecv, serial_dmx_recv, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE(DMX_RECV_TYPE, dmx_recv_init))


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
  if (recv->creator_main_context) {
    g_main_context_unref(recv->creator_main_context);
    recv->creator_main_context = NULL;
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
  self->recv_buffer = 0;
  self->buffer_length[0] = 0;
  self->buffer_length[1] = 0;
  self->read_main_loop = NULL;
  self->read_main_context = NULL;
  self->creator_main_context = g_main_context_ref_thread_default();
  memset(self->change_bits, ~0, MAX_BUFFER/sizeof(guint8));
  self->frame_count = 0;
}

static gboolean
send_packet_signal(gpointer fdata)
{
  SerialDMXRecv *recv = fdata;
  guint b;
  g_rw_lock_reader_lock (&recv->read_buffer_lock);
  b = recv->recv_buffer ^ 1;
  g_signal_emit_by_name(recv, "new-packet",
			recv->buffer_length[b],
			recv->buffers[b]);
  memset(recv->change_bits, 0, MAX_BUFFER/sizeof(guint8));
  g_rw_lock_reader_unlock (&recv->read_buffer_lock);
  return FALSE;
}

/* Sets a bit for every data byte that has changed. Returns true if
   there are changes. */
static guint
signal_changes(SerialDMXRecv *recv)
{
  guint pos = 0;
  guint rb = recv->recv_buffer;
  guint8 *new_data = &recv->buffers[rb][pos];
  guint new_length = recv->buffer_length[rb];
  guint8 *old_data = &recv->buffers[rb ^ 1][pos];
  guint old_length = recv->buffer_length[rb ^ 1];
  while(pos < new_length) {
    if (pos >= old_length || *new_data != *old_data) {
      recv->change_bits[pos/32] |= 1<<(pos % 32);
    }
    new_data++;
    old_data++;
    pos++;
  }
  for(pos = 0; pos < MAX_CHANGE_BIT_WORDS; pos++) {
    if (recv->change_bits[pos] != 0) return TRUE;
  }
  return FALSE;
}

/**
 * serial_dmx_recv_channels_changed:
 * @recv: a SerialDMXRecv object
 * @from: check from this channel, inclusive
 * @to: check up to this channel, exclusive
 *
 * Check if any channel in the range [@to - @from[ has changed since last
 * signal emission.
 *
 * Returns: TRUE if any channel in the range has changed.
 */

/* Set all bits below the given bit position */
#define LOW_MASK(b) ((((guint32)1) <<(b))-1)
/* Set all bits above and including the given bit position */
#define HIGH_MASK(b) (~LOW_MASK(b))

static gboolean
serial_dmx_recv_channels_changed(DMXRecv *base, guint from, guint to)
{
  SerialDMXRecv *recv = SERIAL_DMX_RECV(base);
  guint pos;
  guint end;
  guint b = recv->recv_buffer ^ 1;
  if (to > recv->buffer_length[b]) to = recv->buffer_length[b];
  if (from >= to) return FALSE;
  pos = from / 32;
  from %= 32;
  end = to /32;
  to %= 32;
  if (pos == end) {
    return recv->change_bits[pos] & (HIGH_MASK(from) & LOW_MASK(to));
  } else {
    if (recv->change_bits[pos] & HIGH_MASK(from)) return TRUE;
    while(++pos < end) if (recv->change_bits[pos] != 0) return TRUE;
    return recv->change_bits[end] & LOW_MASK(to);
  }
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
	GSource *source;
	/* If the signal handlers are busy just skip and wait for the next
	   packet. */
	if (g_rw_lock_writer_trylock (&recv->read_buffer_lock)) {
	  recv->recv_buffer ^= 1;  /* Switch buffer */
	  if (recv->frame_count > 2 && signal_changes(recv)) {
	    
	    source = g_idle_source_new();
	    g_object_ref(recv);
	    g_source_set_callback(source, send_packet_signal, recv,g_object_unref);
	    g_source_attach(source, recv->creator_main_context);
	  }
	  g_rw_lock_writer_unlock(&recv->read_buffer_lock);
	}
	recv->frame_count++;
	recv->buffer_length[recv->recv_buffer] = 0;
	

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
      guint rb = recv->recv_buffer;
      state &= ~(STATE_ESCAPE | STATE_FRAMING|STATE_FIRST);
      if (recv->buffer_length[rb] < MAX_BUFFER) {
	recv->buffers[rb][recv->buffer_length[rb]++] = c;
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
