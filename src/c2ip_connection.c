#include "c2ip_connection.h"
#include "c2ip.h"

GQuark
c2ip_connection_error_quark()
{
  static GQuark error_quark = 0;
  if (error_quark == 0)
    error_quark = g_quark_from_static_string ("c2ip-connection-error-quark");
  return error_quark;
}

enum {
  CONNECTION_CLOSED,
  LAST_SIGNAL
};

/* static guint c2ip_connection_signals[LAST_SIGNAL] = {0 }; */

enum
{
  PROP_0 = 0,
  N_PROPERTIES
};

struct _C2IPConnection
{
  GObject parent_instance;
  GCancellable *cancellable;
  GSocketClient *client;
  GSocketConnection *connection;
};

struct _C2IPConnectionClass
{
  GObjectClass parent_class;
  
  /* class members */

  /* Signals */

};

G_DEFINE_TYPE (C2IPConnection, c2ip_connection, G_TYPE_OBJECT)

static void
dispose(GObject *gobj)
{
  C2IPConnection *conn = C2IP_CONNECTION(gobj);
  if (conn->cancellable) {
    g_cancellable_cancel(conn->cancellable);
    g_clear_object(&conn->cancellable);
  }

  g_clear_object(&conn->connection);
  g_clear_object(&conn->client);
  G_OBJECT_CLASS(c2ip_connection_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  /* C2IPConnection *conn = C2IP_CONNECTION(gobj); */
  G_OBJECT_CLASS(c2ip_connection_parent_class)->finalize(gobj);
}

static void
set_property (GObject *object, guint property_id,
	      const GValue *value, GParamSpec *pspec)
{
  /* C2IPConnection *conn = C2IP_CONNECTION_MANAGER(object); */
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
  /* C2IPConnection *conn = C2IP_CONNECTION_MANAGER(object); */
  switch (property_id) {
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
c2ip_connection_class_init (C2IPConnectionClass *klass)
{
  GParamSpec *properties[N_PROPERTIES];
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
  /* C2IPConnectionClass *conn_class = C2IP_CONNECTION_CLASS(klass); */
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
  gobject_class->set_property = set_property;
  gobject_class->get_property = get_property;

  properties[0] = NULL;
  g_object_class_install_properties(gobject_class, N_PROPERTIES, properties);

}

static void
c2ip_connection_init(C2IPConnection *conn)
{
  conn->cancellable = g_cancellable_new();
  conn->client = NULL;
  conn->connection = NULL;
}

C2IPConnection *
c2ip_connection_new(void)
{
  C2IPConnection *conn = g_object_new (C2IP_CONNECTION_TYPE, NULL);
  return conn;
}
