#include "c2ip_connection_manager.h"
#include "c2ip_connection_manager_marshal.h"
#include "c2ip_connection.h"
#include "c2ip.h"
#include "string.h"

GQuark
c2ip_connection_manager_error_quark()
{
  static GQuark error_quark = 0;
  if (error_quark == 0)
    error_quark = g_quark_from_static_string ("c2ip-connection-manager-error-quark");
  return error_quark;
}

enum {
  NEW_CONNECTION,
  LAST_SIGNAL
};

static guint c2ip_connection_manager_signals[LAST_SIGNAL] = {0 };

enum
{
  PROP_0 = 0,
  N_PROPERTIES
};

typedef struct _DeviceList DeviceList;
struct _DeviceList
{
  DeviceList *next;
  DeviceList **prevp;
  guint type;
  gchar *name;
  guint slot;
  GInetSocketAddress *addr;
  C2IPConnection *connection;
};

struct _C2IPConnectionManager
{
  GObject parent_instance;
  DeviceList *devices;
  DeviceList *connecting_device;
  GCancellable *cancellable;
  GSocketClient *client;
  GSocketConnection *connection;
  guint idle_id;
  guint8 reply_buffer[20];
};

struct _C2IPConnectionManagerClass
{
  GObjectClass parent_class;
  
  /* class members */

  /* Signals */
  void (*new_connection)(C2IPConnectionManager *cm,
			 C2IPConnection *conn,
			 guint device_type, const gchar *name, guint slot);
};

G_DEFINE_TYPE (C2IPConnectionManager, c2ip_connection_manager, G_TYPE_OBJECT)

static void
destroy_device(DeviceList *dev)
{
  if (dev->next) dev->next->prevp = dev->prevp;
  *dev->prevp = dev->next;
  g_free(dev->name);
  if (dev->addr)
    g_object_unref(dev->addr);
  if (dev->connection) {
    c2ip_connection_close(dev->connection);
    g_object_unref(dev->connection);
  }
  g_free(dev);
}

static const guint slot_sequence[] = {4,2,0};

static guint
next_slot(guint slot)
{
  const guint *s = slot_sequence;
  while(*s != 0 && *s != slot) s++;
  if (*s != 0) return s[1];
  return 0;
}

static void
dispose(GObject *gobj)
{
  C2IPConnectionManager *cm = C2IP_CONNECTION_MANAGER(gobj);
  if (cm->idle_id > 0) {
    g_source_remove(cm->idle_id);
    cm->idle_id = 0;
  }
  if (cm->cancellable) {
    g_cancellable_cancel(cm->cancellable);
    g_clear_object(&cm->cancellable);
  }

  g_clear_object(&cm->connection);
  g_clear_object(&cm->client);
  while(cm->devices) destroy_device(cm->devices);
  G_OBJECT_CLASS(c2ip_connection_manager_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  /* C2IPConnectionManager *cm = C2IP_CONNECTION_MANAGER(gobj); */
  G_OBJECT_CLASS(c2ip_connection_manager_parent_class)->finalize(gobj);
}

static void
set_property (GObject *object, guint property_id,
	      const GValue *value, GParamSpec *pspec)
{
  /* C2IPConnectionManager *cm = C2IP_CONNECTION_MANAGER(object); */
  switch (property_id)
    {
    default:
       /* We don't have any other property... */
       G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
get_property (GObject *object, guint property_id,
	      GValue *value, GParamSpec *pspec)
{
  /* C2IPConnectionManager *cm = C2IP_CONNECTION_MANAGER(object); */
  switch (property_id) {
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
c2ip_connection_manager_class_init (C2IPConnectionManagerClass *klass)
{
  /* GParamSpec *properties[N_PROPERTIES]; */
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
  /* C2IPConnectionManagerClass *cm_class = C2IP_CONNECTION_MANAGER_CLASS(klass); */
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
  gobject_class->set_property = set_property;
  gobject_class->get_property = get_property;
  c2ip_connection_manager_signals[NEW_CONNECTION] =
    g_signal_new("new-connection",
		 G_OBJECT_CLASS_TYPE (gobject_class),
		 G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(C2IPConnectionManagerClass, new_connection),
		 NULL, NULL,
		 c2ip_connection_manager_marshal_VOID__OBJECT_UINT_STRING_UINT,
		 G_TYPE_NONE, 4,
		 C2IP_CONNECTION_TYPE, G_TYPE_UINT, G_TYPE_STRING,G_TYPE_UINT);
  
#if 0
  properties[0] = NULL;
  g_object_class_install_properties(gobject_class, N_PROPERTIES, properties);
#endif

}

static void
c2ip_connection_manager_init(C2IPConnectionManager *cm)
{
  cm->devices = NULL;
  cm->connecting_device = NULL;
  cm->cancellable = g_cancellable_new();
  cm->client = NULL;
  cm->connection = NULL;
  cm->idle_id = 0;
}



C2IPConnectionManager *
c2ip_connection_manager_new(void)
{
  C2IPConnectionManager *cm = g_object_new (C2IP_CONNECTION_MANAGER_TYPE, NULL);
  return cm;
}

static void
setup_next_connection(C2IPConnectionManager *cm);

gboolean idle_next_connection(gpointer user_data)
{
  C2IPConnectionManager *cm = user_data;
  cm->idle_id = 0;
  setup_next_connection(cm);
  return FALSE;
}

static void
cleanup_connection(C2IPConnectionManager *cm)
{
  cm->connecting_device = NULL;
  if (cm->connection) {
    g_io_stream_close(G_IO_STREAM(cm->connection),NULL, NULL);
  }
  g_clear_object(&cm->connection);
  g_clear_object(&cm->client);
  g_idle_add(idle_next_connection,cm);
}

static void
fail_connection(C2IPConnectionManager *cm)
{
  if (!cm->connecting_device) return;
  destroy_device(cm->connecting_device);
  cleanup_connection(cm);
}

static void
connection_closed(C2IPConnection *conn, C2IPConnectionManager *cm)
{
  DeviceList *dev = cm->devices;
  while(dev) {
    if (dev->connection == conn) {
      destroy_device(dev);
      break;
    }
    dev = dev->next;
  }
  g_debug("Connection closed");
}

static void
handle_data(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GSocketAddress *addr;
  DeviceList *dev;
  gssize r;
  GError *err = NULL;
  C2IPConnectionManager *cm = user_data;
  r = g_input_stream_read_finish(G_INPUT_STREAM(source_object), res, &err);
  if (r == -1) {
    g_warning("Failed to read data: %s", err->message);
    g_clear_error(&err);
    fail_connection(cm);
    return;
  }
  if (cm->reply_buffer[4] != 0x02) {
    g_warning("Invalid reply");
    fail_connection(cm);
    return;
  }
  dev = cm->connecting_device;
  
  if (cm->reply_buffer[5] == 0x00) {
    g_debug("Device %s %d (slot %d) is off-line", dev->name, dev->type, dev->slot);
    dev->slot = next_slot(dev->slot);
    if (dev->slot == 0) {
      fail_connection(cm);
    } else {
      /* Try again with next slot */
      cleanup_connection(cm);
    }
    return;
  }
  g_debug("Got port: %d",C2IP_U16(&cm->reply_buffer[6]));

  addr = g_inet_socket_address_new(g_inet_socket_address_get_address(dev->addr),
				   C2IP_U16(&cm->reply_buffer[6]));
  dev->connection = c2ip_connection_new(G_INET_SOCKET_ADDRESS(addr), dev->slot);
  g_object_unref(addr);
  g_signal_connect_object(dev->connection, "connection-closed",
			  (GCallback)connection_closed, cm,
			  0);
  g_signal_emit(cm, c2ip_connection_manager_signals[NEW_CONNECTION], 0,
		dev->connection, dev->type, dev->name, dev->slot);
  cleanup_connection(cm);
}

static void
handle_header(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  guint type;
  guint length;
  gssize r;
  GError *err = NULL;
  C2IPConnectionManager *cm = user_data;
  r = g_input_stream_read_finish(G_INPUT_STREAM(source_object), res, &err);
  if (r == -1) {
    g_warning("Failed to read header: %s", err->message);
    g_clear_error(&err);
    fail_connection(cm);
    return;
  }
  if (r < 4){
    g_warning("Short header");
    fail_connection(cm);
    return;
  }
  type = C2IP_U16(&cm->reply_buffer[0]);
  if (type != 0x0001) {
    g_warning("Wrong packet type");
    fail_connection(cm);
    return;
  }
  length = C2IP_U16(&cm->reply_buffer[2]);
  if (length != 4) {
    g_warning("Reply has wrong size");
    fail_connection(cm);
    return;
  }
  g_input_stream_read_async(G_INPUT_STREAM(source_object),
			    cm->reply_buffer + 4, 4, G_PRIORITY_LOW, cm->cancellable, handle_data, cm);
}

static void
connect_callback(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  gsize written;
  static guint8 request_templ[] = {0x00, 0x01, 0x00, 0x03, 0x01, 0x00,0x04};
  guint8 request[sizeof(request_templ)];
  GOutputStream *out;
  GInputStream *in;
  GError *err = NULL;
  C2IPConnectionManager *cm = user_data;
  cm->connection = g_socket_client_connect_finish(cm->client, res, &err);
  if (!cm->connection) {
    g_warning("Connection failed: %s", err->message);
    g_clear_error(&err);
    fail_connection(cm);
    return;
  }
  memcpy(request, request_templ, sizeof(request));
  C2IP_U16_SET(request + 5, cm->connecting_device->slot);
  out = g_io_stream_get_output_stream(G_IO_STREAM(cm->connection));
  if (!g_output_stream_write_all(out, request,sizeof(request), &written,
				 cm->cancellable, &err)) {
    g_warning("Failed to write request");
    g_clear_error(&err);
    fail_connection(cm);
    return;
  }
  in = g_io_stream_get_input_stream(G_IO_STREAM(cm->connection));
  g_input_stream_read_async(in, cm->reply_buffer, 4, G_PRIORITY_LOW, cm->cancellable, handle_header, cm);
}

static void
setup_next_connection(C2IPConnectionManager *cm)
{
  DeviceList *dev = cm->devices;
  while(dev) {
    if (dev->connection == NULL) break;
    dev = dev->next;
  }
  if (!dev) return;
  if (cm->connecting_device) return;	/* Connection already in progress */
  cm->connecting_device = dev;
  cm->client = g_socket_client_new();
  g_socket_client_set_timeout (cm->client, 15);
  g_socket_client_connect_async(cm->client, G_SOCKET_CONNECTABLE(dev->addr),
				cm->cancellable, connect_callback, cm);
}

gboolean
c2ip_connection_manager_add_device(C2IPConnectionManager *cm,
				   guint type, const gchar *name,
				   GInetAddress *addr, guint port)
{
  DeviceList *dev = cm->devices;
  while(dev) {
    if (dev->type == type && strcmp(dev->name, name) == 0
	&& g_inet_address_equal(g_inet_socket_address_get_address(dev->addr),
				  addr)
	&& g_inet_socket_address_get_port(dev->addr) == port) {
      return TRUE;
    }
    dev = dev->next;
  }
  dev = g_new(DeviceList,1);
  dev->type = type;
  dev->name = g_strdup(name);
  dev->slot = slot_sequence[0];
  dev->addr = G_INET_SOCKET_ADDRESS(g_inet_socket_address_new(addr,port));
  dev->connection = NULL;
  dev->next = cm->devices;
  dev->prevp = &cm->devices;
  if (cm->devices) cm->devices->prevp = &dev->next;
  cm->devices = dev;
  setup_next_connection(cm);
  return TRUE;
}

C2IPConnection *
c2ip_connection_manager_get_connection(const C2IPConnectionManager *cm,
				       guint type, const gchar *name)
{
  DeviceList *d = cm->devices;
  while(d) {
    if (type == d->type && strcmp(name, d->name) == 0) return d->connection;
    d = d->next;
  }
  return NULL;
}
