#include "dmx_recv.h"
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <dmx_serial.h>
#include <stdint.h>
#include <stdio.h>


struct _DMXRecv
{
  GObject parent_instance;
  int serial_fd;
};
struct _DMXRecvClass
{
  GObjectClass parent_class;

  /* class members */
};

G_DEFINE_TYPE (DMXRecv, dmx_recv, G_TYPE_OBJECT)

static void
dispose(GObject *gobj)
{
  DMXRecv *recv = DMX_RECV(gobj);
  if (recv->serial_fd >= 0) {
    close(recv->serial_fd);
    recv->serial_fd = -1;
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
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
}

static void
dmx_recv_init (DMXRecv *self)
{
  self->serial_fd = -1;
}

struct DMXSource
{
  GSource source;
  GPollFD pollfd;
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
  int i;
  uint8_t buffer[520];
  struct DMXSource *dmx_source = (struct DMXSource*)source;
  ssize_t r = read(dmx_source->pollfd.fd, buffer, sizeof(buffer));
  g_debug("Dispatch");
  for (i = 0; i < r; i++) {
    fprintf(stderr, " %02x", buffer[i]);
  }
   fprintf(stderr, "\n");
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
  g_source_add_poll(&source->source, &((struct DMXSource*)source)->pollfd);
  g_source_attach(&source->source, NULL);
  return recv;
}
