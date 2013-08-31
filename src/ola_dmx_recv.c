#include "ola_dmx_recv.h"
#include <ola_wrapper.h>
#include <buffered_dmx_recv_private.h>
#include <string.h>

#define MAX_BUFFER 512

struct _OLADMXRecv
{
  BufferedDMXRecv parent_instance;
  OLAWrapper *ola_wrapper;
  guint ola_universe;
  
  GThread *read_thread;
  GCond read_start_cond;
  GMutex read_mutex;
  guint frame_count;
  GError *read_error;
};

struct _OLADMXRecvClass
{
  BufferedDMXRecvClass parent_class;

  /* class members */

};

G_DEFINE_TYPE(OLADMXRecv, ola_dmx_recv, BUFFERED_DMX_RECV_TYPE)

static void
stop_read_thread(OLADMXRecv *recv);

static void
dispose(GObject *gobj)
{
  OLADMXRecv *recv = OLA_DMX_RECV(gobj);
  stop_read_thread(recv);
  G_OBJECT_CLASS(ola_dmx_recv_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  G_OBJECT_CLASS(ola_dmx_recv_parent_class)->finalize(gobj);
}

static void
ola_dmx_recv_class_init (OLADMXRecvClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;

}

static void
ola_dmx_recv_init (OLADMXRecv *self)
{
  self->ola_wrapper = NULL;
  self->read_thread = NULL;
  self->frame_count = 0;
}

static void
new_packet(const guint8 *data, gsize len, gpointer user_data)
{
  guint8 *buffer;
  BufferedDMXRecv *recv = user_data;
  buffer = buffered_dmx_recv_start_write(recv);
  memcpy(buffer, data, len);
  buffered_dmx_recv_end_write(recv, len);
}

static gpointer
read_thread(gpointer data)
{
  OLADMXRecv *recv = data;
  g_mutex_lock(&recv->read_mutex);
  recv->ola_wrapper = ola_wrapper_read_new(new_packet, recv, recv->ola_universe, &recv->read_error);
  if (!recv->ola_wrapper) {
    g_mutex_unlock(&recv->read_mutex);
    g_cond_signal(&recv->read_start_cond);
    return NULL;
  }
  g_mutex_unlock(&recv->read_mutex);
  g_cond_signal(&recv->read_start_cond);
  g_debug("Read thread started");
  ola_wrapper_run(recv->ola_wrapper);
  g_debug("Read thread stopped");

  g_mutex_lock(&recv->read_mutex);
  ola_wrapper_terminate(recv->ola_wrapper);
  ola_wrapper_destroy(recv->ola_wrapper);
  recv->ola_wrapper = NULL;
  g_mutex_unlock(&recv->read_mutex);
  return NULL; 
}

static gboolean
start_read_thread(OLADMXRecv *recv,GError **err)
{
  if (!recv->ola_wrapper) {
    recv->read_error = NULL;
    g_cond_init(&recv->read_start_cond);
    g_mutex_init(&recv->read_mutex);
    recv->read_thread = g_thread_new("OLA", read_thread, recv);
    g_mutex_lock (&recv->read_mutex);
    while(!recv->ola_wrapper && !recv->read_error) {
      g_cond_wait(&recv->read_start_cond, &recv->read_mutex);
    }
    g_mutex_unlock (&recv->read_mutex);
    g_cond_clear(&recv->read_start_cond);
    if (recv->read_error) {
      g_propagate_error(err, recv->read_error);
      recv->read_error = NULL;
      return FALSE;
    }
  }
  return TRUE;
}

static void
stop_read_thread(OLADMXRecv *recv)
{
  if (recv->read_thread) {
    g_mutex_lock (&recv->read_mutex);
    if (recv->ola_wrapper) {
      ola_wrapper_terminate(recv->ola_wrapper);
    }
    g_mutex_unlock (&recv->read_mutex);
    g_thread_join(recv->read_thread);
    recv->read_thread = NULL;
    g_mutex_clear(&recv->read_mutex);
  }
}

DMXRecv *
ola_dmx_recv_new(guint universe, GError **err)
{
  OLADMXRecv *recv = g_object_new (OLA_DMX_RECV_TYPE, NULL);
  recv->ola_universe = universe;
  if (!start_read_thread(recv, err)) {
    g_object_unref(recv);
    return NULL;
  }
  return DMX_RECV(recv);
}
