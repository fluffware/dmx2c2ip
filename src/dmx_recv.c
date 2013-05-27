#include "dmx_recv.h"
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

struct _DMXRecv
{
  GObject parent_instance;
  int serial_fd;
  gint source;
  guint state;
  guint8 buffers[2][MAX_BUFFER];
  guint recv_buffer;
  guint buffer_length[2];
};

struct _DMXRecvClass
{
  GObjectClass parent_class;

  /* class members */

  /* Signals */
  /* value = channel_changed & 0xff
     channel = channel_value >> 8;
  */
  void (*channel_changed)(DMXRecv *recv, gint channel_value);
  void (*new_packet)(DMXRecv *recv, guint l, guint8 *data);
};

enum {
  CHANNEL_CHANGED,
  NEW_PACKET,
  LAST_SIGNAL
};

static guint dmx_recv_signals[LAST_SIGNAL] = {0 };

G_DEFINE_TYPE (DMXRecv, dmx_recv, G_TYPE_OBJECT)

static void
channel_changed(DMXRecv *recv, gint channel_value)
{
  fprintf(stderr, "%d: %02x\n",channel_value>>8, channel_value & 0xff);
}
static void
new_packet(DMXRecv *recv, guint l, guint8 *data)
{
  while(l-- > 0) {
    fprintf(stderr, " %02x",*data++);
  }
  fprintf(stderr, "\n");
}

static void
dispose(GObject *gobj)
{
  DMXRecv *recv = DMX_RECV(gobj);
  if (recv->serial_fd >= 0) {
    close(recv->serial_fd);
    recv->serial_fd = -1;
  }
  if (recv->source) {
    g_source_remove(recv->source);
    recv->source = 0;
  }
  G_OBJECT_CLASS(dmx_recv_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  G_OBJECT_CLASS(dmx_recv_parent_class)->finalize(gobj);
}

static void
dmx_recv_class_init (DMXRecvClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  DMXRecvClass *recv_class = DMX_RECV_CLASS(klass);
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;

  recv_class->channel_changed = channel_changed;
  recv_class->new_packet = new_packet;
  
  dmx_recv_signals[CHANNEL_CHANGED] =
    g_signal_new("channel-changed",
		 G_OBJECT_CLASS_TYPE (gobject_class), G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(DMXRecvClass, channel_changed),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__UINT,
		 G_TYPE_NONE, 1, G_TYPE_INT);
  dmx_recv_signals[NEW_PACKET] =
    g_signal_new("new-packet",
		 G_OBJECT_CLASS_TYPE (gobject_class), G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(DMXRecvClass, new_packet),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__UINT_POINTER,
		 G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);
}

static void
dmx_recv_init (DMXRecv *self)
{
  self->serial_fd = -1;
  self->source = 0;
  self->state = STATE_NORMAL;
  self->recv_buffer = 0;
  self->buffer_length[0] = 0;
  self->buffer_length[1] = 0;
}

/* Send a signal for every data byte that has changed */

static void
signal_changes(DMXRecv *recv, guint pos)
{
  guint rb = recv->recv_buffer;
  guint8 *new_data = &recv->buffers[rb][pos];
  guint new_length = recv->buffer_length[rb];
  guint8 *old_data = &recv->buffers[rb ^ 1][pos];
  guint old_length = recv->buffer_length[rb ^ 1];
  while(pos < new_length) {
    if (pos >= old_length || *new_data != *old_data) {
      g_signal_emit(recv, dmx_recv_signals[CHANNEL_CHANGED], 0, pos <<8 | *new_data);
    }
    new_data++;
    old_data++;
    pos++;
  }
}

/* Decode received data and put it in the right buffer. Switch buffer on break.
 */
static void
handle_data(DMXRecv *recv, const uint8_t *data, gsize data_length)	    
{
  guint from = recv->buffer_length[recv->recv_buffer];
  guint state = recv->state;
  while(data_length > 0) {
    uint8_t c = *data++;
    data_length--;
    if (state & STATE_FIRST) {
      if (c != 0) state |= STATE_SKIP;
      state &= ~STATE_FIRST;
    } else if (state & STATE_FRAMING) {
      if (c == 0x00) {
	guint rb = recv->recv_buffer;
	signal_changes(recv, from);
	g_signal_emit(recv, dmx_recv_signals[NEW_PACKET], 0,
		      recv->buffer_length[rb],
		      recv->buffers[rb]);
	recv->recv_buffer ^= 1;  /* Switch buffer */
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
  signal_changes(recv, from);
  recv->state = state;
}

struct DMXSource
{
  GSource source;
  GPollFD pollfd;
  DMXRecv *recv;
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
  g_debug("Dispatch");
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

DMXRecv *
dmx_recv_new(const char *device, GError **err)
{
  struct DMXSource *source;
  DMXRecv *recv = g_object_new (DMX_RECV_TYPE, NULL);
  recv->serial_fd = dmx_serial_open(device, err);
  if (recv->serial_fd < 0) {
    g_object_unref(recv);
    return NULL;
  }
  source =
    (struct DMXSource*)g_source_new(&source_funcs, sizeof(struct DMXSource));
  g_assert(source != NULL);
  source->pollfd.events = G_IO_IN | G_IO_ERR;
  source->pollfd.fd = recv->serial_fd;
  source->recv = recv;
  g_source_add_poll(&source->source, &((struct DMXSource*)source)->pollfd);
  recv->source = g_source_attach(&source->source, NULL);
  return recv;
}
