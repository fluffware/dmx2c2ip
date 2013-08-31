#include "dmx_recv.h"
#include <unistd.h>

enum {
  CHANNEL_CHANGED,
  NEW_PACKET,
  LAST_SIGNAL
};

static guint dmx_recv_signals[LAST_SIGNAL] = {0 };
G_DEFINE_INTERFACE (DMXRecv, dmx_recv, G_TYPE_OBJECT)

static gboolean
channels_changed(DMXRecv *recv, guint from, guint to)
{
  g_critical("DMXRecv::channels_changed not implemented");
  return FALSE;
}

static void
dmx_recv_default_init (DMXRecvInterface *iface)
{
  static gboolean initialized = FALSE;
  if (!initialized) {
    dmx_recv_signals[CHANNEL_CHANGED] =
      g_signal_new("channel-changed",
		   G_TYPE_FROM_INTERFACE (iface), G_SIGNAL_RUN_LAST,
		   G_STRUCT_OFFSET(DMXRecvInterface, channel_changed),
		   NULL, NULL,
		   g_cclosure_marshal_VOID__UINT,
		   G_TYPE_NONE, 1, G_TYPE_INT);
    dmx_recv_signals[NEW_PACKET] =
      g_signal_new("new-packet",
		   G_TYPE_FROM_INTERFACE (iface), G_SIGNAL_RUN_LAST,
		   G_STRUCT_OFFSET(DMXRecvInterface, new_packet),
		   NULL, NULL,
		   g_cclosure_marshal_VOID__UINT_POINTER,
		   G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);
    
    initialized = TRUE;
  }
  iface->channels_changed = channels_changed;
}

/**
 * dmx_recv_channels_changed:
 * @recv: a DMXRecv object
 * @from: check from this channel, inclusive
 * @to: check up to this channel, exclusive
 *
 * Check if any channel in the range [@to - @from[ has changed since last
 * signal emission.
 *
 * Returns: TRUE if any channel in the range has changed.
 */

gboolean
dmx_recv_channels_changed(DMXRecv *recv, guint from, guint to)
{
  DMXRecvInterface *iface = DMX_RECV_GET_INTERFACE(recv);
  return iface->channels_changed(recv, from, to);
}
