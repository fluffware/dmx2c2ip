#include "buffered_dmx_recv_private.h"
#include <string.h>

static gboolean
buffered_dmx_recv_channels_changed(DMXRecv *recv, guint from, guint to);

static void
dmx_recv_init(DMXRecvInterface *iface) {
  iface->channels_changed = buffered_dmx_recv_channels_changed;
}

G_DEFINE_TYPE_WITH_CODE (BufferedDMXRecv, buffered_dmx_recv, G_TYPE_OBJECT,
			 G_IMPLEMENT_INTERFACE(DMX_RECV_TYPE, dmx_recv_init))


static void
dispose(GObject *gobj)
{
  BufferedDMXRecv *recv = BUFFERED_DMX_RECV(gobj);
  if (recv->idle_source) {
    g_source_destroy(recv->idle_source);
    recv->idle_source = NULL;
  }
  if (recv->creator_main_context) {
    g_main_context_unref(recv->creator_main_context);
    recv->creator_main_context = NULL;
  }
  g_free(recv->read_buffer);
  recv->read_buffer = NULL;
  g_free(recv->write_buffer);
  recv->write_buffer = NULL;
  G_OBJECT_CLASS(buffered_dmx_recv_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  BufferedDMXRecv *recv = BUFFERED_DMX_RECV(gobj);
  g_mutex_clear (&recv->buffer_mutex);
  G_OBJECT_CLASS(buffered_dmx_recv_parent_class)->finalize(gobj);
}

static void
buffered_dmx_recv_class_init (BufferedDMXRecvClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
}

static void
buffered_dmx_recv_init (BufferedDMXRecv *self)
{
  self->read_length = 0;
  self->write_buffer = g_new(guint8, MAX_BUFFER);
  self->write_length = 0;
  self->read_buffer = g_new(guint8, MAX_BUFFER);
  self->creator_main_context = g_main_context_ref_thread_default();
  memset(self->change_bits, ~0, MAX_BUFFER/sizeof(guint8));
  g_mutex_init (&self->buffer_mutex);
}

static void
swap_buffers(BufferedDMXRecv *recv)
{
  gsize tl;
  guint8 *tb =recv->read_buffer;
  recv->read_buffer = recv->write_buffer;
  recv->write_buffer = tb;
  tl = recv->read_length;
  recv->read_length = recv->write_length;
  recv->write_length = tl;
}

/* Sets a bit for every data byte that has changed. Returns true if
   there are changes. */
static guint
channels_changed(BufferedDMXRecv *recv)
{
  guint pos = 0;
  guint8 *new_data = recv->write_buffer;
  guint new_length = recv->write_length;
  guint8 *old_data = recv->read_buffer;
  guint old_length = recv->read_length;
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

static gboolean
send_packet_signal(gpointer fdata)
{
  BufferedDMXRecv *recv = fdata;
  
  /* If the buffer is locked then try again later */
  if (!g_mutex_trylock(&recv->buffer_mutex)) return TRUE;
  recv->idle_source = NULL;
  if (!channels_changed(recv)) {
    /* Nothing changed */
    g_mutex_unlock(&recv->buffer_mutex);
    return FALSE;
  }
  swap_buffers(recv);
  g_mutex_unlock(&recv->buffer_mutex);
  g_signal_emit_by_name(recv, "new-packet",
			recv->read_length, recv->read_buffer);
  memset(recv->change_bits, 0, MAX_BUFFER/sizeof(guint8));
  return FALSE;
}


/**
 * buffered_dmx_recv_channels_changed:
 * @recv: a BufferedDMXRecv object
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
buffered_dmx_recv_channels_changed(DMXRecv *base, guint from, guint to)
{
  BufferedDMXRecv *recv = BUFFERED_DMX_RECV(base);
  guint pos;
  guint end;
  if (to > recv->read_length) to = recv->read_length;
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

guint8*
buffered_dmx_recv_start_write(BufferedDMXRecv *recv)
{
  g_mutex_lock(&recv->buffer_mutex);
  return recv->write_buffer;
}

void
buffered_dmx_recv_end_write(BufferedDMXRecv *recv, gsize len)
{
  recv->write_length = len;
  if (!recv->idle_source) {
    recv->idle_source = g_idle_source_new();
    g_object_ref(recv);
    g_source_set_callback(recv->idle_source, send_packet_signal, recv,
			  g_object_unref);
    g_source_attach(recv->idle_source, recv->creator_main_context);    
  }
  g_mutex_unlock(&recv->buffer_mutex);
}
