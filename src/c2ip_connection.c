#include "c2ip_connection.h"
#include "c2ip.h"
#include <string.h>
#include <stdio.h>

GQuark
c2ip_connection_error_quark()
{
  static GQuark error_quark = 0;
  if (error_quark == 0)
    error_quark = g_quark_from_static_string ("c2ip-connection-error-quark");
  return error_quark;
}

enum {
  CONNECTED,
  CONNECTION_CLOSED,
  RECEIVED_PACKET,
  LAST_SIGNAL
};

static guint c2ip_connection_signals[LAST_SIGNAL] = {0 };

enum
{
  PROP_0 = 0,
  PROP_CLIENT_NAME,
  PROP_PING_REPLY,
  PROP_PING_INTERVAL,
  PROP_SLOT,
  N_PROPERTIES
};

struct _C2IPConnection
{
  GObject parent_instance;
  gchar *client_name;
  guint slot;
  GCancellable *cancellable;
  GSocketClient *client;
  GSocketConnection *connection;
  guint8 reply_buffer[256];
  guint reply_pos;
  gboolean ping_reply; /* Automatically reply to ping requests */
  guint ping_interval; /* Automaticaly send pings at the specified
			  interval (in ms). Set to 0 to disable.
		       */
  guint ping_timeout_id;
};

struct _C2IPConnectionClass
{
  GObjectClass parent_class;
  
  /* class members */

  /* Signals */
  void (*connection_closed)(C2IPConnection *conn);
  void (*connected)(C2IPConnection *conn);
  void (*received_packet)(C2IPConnection *conn, guint length, const guint8 *packet);
};

G_DEFINE_TYPE (C2IPConnection, c2ip_connection, G_TYPE_OBJECT)

static void
dispose(GObject *gobj)
{
  C2IPConnection *conn = C2IP_CONNECTION(gobj);
  if (conn->ping_timeout_id > 0) {
    g_source_remove(conn->ping_timeout_id);
    conn->ping_timeout_id = 0;
  }
  if (conn->cancellable) {
    g_cancellable_cancel(conn->cancellable);
    g_clear_object(&conn->cancellable);
  }

  g_clear_object(&conn->connection);
  g_clear_object(&conn->client);
  g_free(conn->client_name);
  G_OBJECT_CLASS(c2ip_connection_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  /* C2IPConnection *conn = C2IP_CONNECTION(gobj); */
  G_OBJECT_CLASS(c2ip_connection_parent_class)->finalize(gobj);
}

static void
start_ping_interval(C2IPConnection *conn);

static void
set_property (GObject *object, guint property_id,
	      const GValue *value, GParamSpec *pspec)
{
  C2IPConnection *conn = C2IP_CONNECTION(object);
  switch (property_id)
    {
    case PROP_CLIENT_NAME:
      g_free(conn->client_name);
      conn->client_name = g_value_dup_string(value);
      break;
    case PROP_PING_REPLY:
      conn->ping_reply = g_value_get_boolean(value);
      break;
    case PROP_PING_INTERVAL:
      {
	guint interval = g_value_get_uint(value);
	if (interval != conn->ping_interval) {
	  conn->ping_interval = interval;
	  start_ping_interval(conn);
	}
      }
      break;
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
  C2IPConnection *conn = C2IP_CONNECTION(object); 
  switch (property_id) {
  case PROP_CLIENT_NAME:
    g_value_set_string(value, conn->client_name);
    break;
  case PROP_PING_REPLY:
    g_value_set_boolean(value, conn->ping_reply);
    break;
  case PROP_PING_INTERVAL:
    g_value_set_uint(value, conn->ping_reply);
    break;
  case PROP_SLOT:
    g_value_set_uint(value, conn->slot);
    break;
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
  
  c2ip_connection_signals[CONNECTION_CLOSED] =
    g_signal_new("connection-closed",
		 G_OBJECT_CLASS_TYPE (gobject_class), G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(C2IPConnectionClass, connection_closed),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__VOID,
		 G_TYPE_NONE, 0);
  
  c2ip_connection_signals[CONNECTED] =
    g_signal_new("connected",
		 G_OBJECT_CLASS_TYPE (gobject_class), G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(C2IPConnectionClass, connected),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__VOID,
		 G_TYPE_NONE, 0);
  c2ip_connection_signals[RECEIVED_PACKET] =
    g_signal_new("received-packet",
		 G_OBJECT_CLASS_TYPE (gobject_class),
		 G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(C2IPConnectionClass, received_packet),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__UINT_POINTER,
		 G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);

  properties[0] = NULL;
  properties[PROP_CLIENT_NAME] =
    g_param_spec_string("client-name", "Client name",
			"Name supplied to device when connecting",
			"C2IP client",
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_PING_REPLY] =
    g_param_spec_boolean("reply-ping", "Auto-reply ping",
			 "Automatically reply to ping requests",
			 TRUE,
			 G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_PING_INTERVAL] =
    g_param_spec_uint("ping-interval", "Ping interval",
		      "Send pings at this interval. Set to 0 to disable.",
		      0, 60000, 5000,
		      G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
   properties[PROP_SLOT] =
    g_param_spec_uint("slot", "Slot",
		      "Slot number used as packet type",
		      0, 100, 0,
		      G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties(gobject_class, N_PROPERTIES, properties);
}

static void
c2ip_connection_init(C2IPConnection *conn)
{
  conn->cancellable = g_cancellable_new();
  conn->client = NULL;
  conn->connection = NULL;
  conn->client_name = g_strdup("C2IP client");
  conn->ping_reply = TRUE;
  conn->ping_interval = 5000;
  conn->ping_timeout_id = 0;
  conn->slot = 0;
}

static void
close_connection(C2IPConnection *conn)
{
  conn->ping_interval = 0;
  start_ping_interval(conn);
  if (conn->connection) {
    g_io_stream_close(G_IO_STREAM(conn->connection),NULL, NULL);
  }
  if (conn->connection) {
      g_signal_emit(conn, c2ip_connection_signals[CONNECTION_CLOSED], 0);
  }
  g_clear_object(&conn->connection);
  g_clear_object(&conn->client);
}

static gboolean
send_request(C2IPConnection *conn, const guint8 *request, gsize length,
	     GError **err)
{
  gsize written;
  GOutputStream *out;
  if (!conn->connection) {
    g_set_error(err, C2IP_CONNECTION_ERROR, C2IP_CONNECTION_ERROR_NO_CONNECTION,
		"No connection");
    return FALSE;
  }
  out = g_io_stream_get_output_stream(G_IO_STREAM(conn->connection));
  if (!g_output_stream_write_all(out, request,length, &written,
				 conn->cancellable, err)) {
    close_connection(conn);
    return FALSE;
  }
  return TRUE;
}

static void
init_request(guint8 *buffer, guint type, gsize length)
{
  buffer[0] = type>>8;
  buffer[1] = type;
  buffer[2] = length >> 8;
  buffer[3] = length;
}
static gboolean
send_ping(C2IPConnection *conn, GError **err)
{
  static const guint8 req[] = {0x00, 0x01, 0x00, 0x01, 0x06};
  return send_request(conn, req, sizeof(req), err);
}

static gboolean
send_ping_reply(C2IPConnection *conn, GError **err)
{
  static const guint8 req[] = {0x00, 0x01, 0x00, 0x01, 0x07};
  return send_request(conn, req, sizeof(req), err);
}

static gboolean
send_authentication(C2IPConnection *conn, const gchar *name, GError **err)
{
  gboolean res;
  gsize slen = strlen(name);
  guint8 *buffer = g_new(guint8,slen + 11);
  init_request(buffer, 0x0001, slen + 7);
  buffer[4] = 0x03;
  C2IP_U16_SET(&buffer[5], conn->slot);
  buffer[7] = slen;
  memcpy(&buffer[8], name, slen);
  buffer[8+slen] = 0x05;
  buffer[9 + slen] = 0x02;
  buffer[10 + slen] = 0x01;
  res = send_request(conn, buffer, slen + 11, err);
  g_free(buffer);
  return res;
}

static void
handle_reply(C2IPConnection *conn, const guint8 *reply, gsize plen)
{
  guint type = C2IP_U16(&reply[0]);
  guint dlen = C2IP_U16(&reply[2]);
  if (conn->ping_reply && type == C2IP_PKT_TYPE_SETUP && dlen == 1 && reply[4] == 0x06) {
    GError *err = NULL;
    if (!send_ping_reply(conn, &err)) {
      g_warning("Failed to send automatic ping reply: %s", err->message);
      g_clear_error(&err);
    }
  } else if (conn->ping_interval > 0 && type == C2IP_PKT_TYPE_SETUP && dlen >= 2 && reply[4] == C2IP_REPLY_AUTH) {
    g_signal_emit(conn, c2ip_connection_signals[CONNECTED], 0);  
    
  } else if (conn->ping_interval > 0 && type == 0x01 && dlen == 1 && reply[4] == 0x07) {
    /* Ignore ping replies if the requests are sent by this object */
  } else {
    g_signal_emit(conn, c2ip_connection_signals[RECEIVED_PACKET], 0,
		  plen, reply);
  }
}

static void
handle_data(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  gssize r;
  GError *err = NULL;
  C2IPConnection *conn = user_data;
  r = g_input_stream_read_finish(G_INPUT_STREAM(source_object), res, &err);
  if (r == -1) {
    if (g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CLOSED)) {
      g_clear_error(&err);
      return;
    }
    g_warning("Failed to read packet: %s", err->message);
    g_clear_error(&err);
    close_connection(conn);
    return;
  }
  if (r == 0) {
    close_connection(conn);
    return;
  }
  conn->reply_pos += r;
  while(conn->reply_pos >= 4) {
    guint length = C2IP_U16(&conn->reply_buffer[2]);
    if (conn->reply_pos < length + 4) break;
    handle_reply(conn, conn->reply_buffer, length + 4);
    conn->reply_pos -= length + 4;
    memmove(conn->reply_buffer, conn->reply_buffer + length + 4,
	    conn->reply_pos);
  }
  
  g_input_stream_read_async(G_INPUT_STREAM(source_object),
			    conn->reply_buffer + conn->reply_pos,
			    sizeof(conn->reply_buffer) - conn->reply_pos,
			    G_PRIORITY_LOW,
			    conn->cancellable, handle_data, conn);
}

static void
read_reply(C2IPConnection *conn)
{
  GInputStream *in;
  in = g_io_stream_get_input_stream(G_IO_STREAM(conn->connection));
  conn->reply_pos = 0;
  g_input_stream_read_async(in, conn->reply_buffer, sizeof(conn->reply_buffer),
			    G_PRIORITY_LOW,
			    conn->cancellable, handle_data, conn);
}

static gboolean
ping_timeout_callback(gpointer user_data)
{
  GError *err = NULL;
  C2IPConnection *conn = user_data;
  if (!send_ping(conn, &err)) {
    g_warning("Failed to send ping: %s", err->message);
    g_clear_error(&err);
  }
  return TRUE;
}

static void
start_ping_interval(C2IPConnection *conn)
{
  if (conn->ping_timeout_id > 0) {
    g_source_remove(conn->ping_timeout_id);
    conn->ping_timeout_id = 0;
  }
  if (conn->ping_interval > 0) {
    conn->ping_timeout_id = g_timeout_add(conn->ping_interval,
					  ping_timeout_callback, conn);
  }
}

static void
connect_callback(GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GError *err = NULL;
  C2IPConnection *conn = user_data;
  conn->connection = g_socket_client_connect_finish(conn->client, res, &err);
  if (!conn->connection) {
    g_warning("Connection failed: %s", err->message);
    g_clear_error(&err);
    close_connection(conn);
    return;
  }
  start_ping_interval(conn);
  if (!send_authentication(conn, conn->client_name, &err)) {
    g_warning("Failed to send authentication: %s", err->message);
    g_clear_error(&err);
    close_connection(conn);
    return;
  }
  read_reply(conn);
}

C2IPConnection *
c2ip_connection_new(GInetSocketAddress *addr, guint slot)
{
  C2IPConnection *conn = g_object_new (C2IP_CONNECTION_TYPE, NULL);
  conn->slot = slot;
  conn->client = g_socket_client_new();
  g_socket_client_connect_async(conn->client, G_SOCKET_CONNECTABLE(addr),
				conn->cancellable, connect_callback, conn);
  return conn;
}

gboolean
c2ip_connection_connected(C2IPConnection *conn)
{
  return conn->connection != NULL;
}

gboolean
c2ip_connection_send_raw_packet(C2IPConnection *conn,
				const guint8 *packet, gsize length,
				GError **err)
{
  g_assert(C2IP_U16(&packet[2]) + 4 == length);
  return send_request(conn, packet, length, err);
}

void
c2ip_connection_close(C2IPConnection *conn)
{
  close_connection(conn);
}

/**
 * c2ip_connection_send_ping:
 * @conn: connection object
 * @err: error
 *
 * Send a ping packet
 *
 * Returns: TRUE if successfull, otherwise @err is set
 **/

gboolean
c2ip_connection_send_ping(C2IPConnection *conn, GError **err)
{
  return send_ping(conn, err);
}

/**
 * c2ip_connection_send_value_request:
 * @conn: connection object
 * @id: id of the function whose value is requested
 * @err: error
 *
 * Send a request for a single function value
 *
 * Returns: TRUE if successfull, otherwise @err is set
 **/

gboolean
c2ip_connection_send_value_request(C2IPConnection *conn, guint16 id,
				   GError **err)
{
  guint8 buffer[] = {0x00, 0x04, 0x00, 0x03, 0x04, 0x00, 0x00};
  C2IP_U16_SET(&buffer[0], conn->slot);  
  C2IP_U16_SET(&buffer[5], id);
  return send_request(conn, buffer, sizeof(buffer), err);
}

/**
 * c2ip_connection_send_value_request_all:
 * @conn: connection object
 * @err: error
 *
 * Send a request for a all function values
 *
 * Returns: TRUE if successfull, otherwise @err is set
 **/

gboolean
c2ip_connection_send_value_request_all(C2IPConnection *conn, GError **err)
{
  guint8 buffer[] = {0x00, 0x04, 0x00, 0x01, 0x00};
  C2IP_U16_SET(&buffer[0], conn->slot);
  return send_request(conn, buffer, sizeof(buffer), err);
}

/**
 * c2ip_connection_send_value_request_all:
 * @conn: connection object
 * @id: id of the function whose value is changed
 * @type: value type. Or with #C2IP_TYPE_FLAG_RELATIVE to make a relative change.
 * @err: error
 *
 * Send a request for a all function values
 *
 * Returns: TRUE if successfull, otherwise @err is set
 **/

gboolean
c2ip_connection_send_value_change(C2IPConnection *conn, guint16 id,
				  guint8 type,
				  guint8 value_length, const guint8 *value,
				  GError **err)
{
  gboolean res;
  guint8 *buffer = g_new(guint8,value_length + 10);
  init_request(buffer, conn->slot, value_length + 6);
  buffer[4] = C2IP_REQUEST_VALUE_CHANGE;
  C2IP_U16_SET(&buffer[5], id);
  buffer[7] = 0x00;
  buffer[8] = type;
  buffer[9] = value_length;
  memcpy(&buffer[10], value, value_length);
  res = send_request(conn, buffer, value_length + 10, err);
  g_free(buffer);
  return res;
}

/**
 * c2ip_connection_send_option_request:
 * @conn: connection object
 * @id: id of the function whose options are requested
 * @err: error
 *
 * Send a request for a list of allowed options for @id.
 * Valid for functions of type BOOL and ENUM.
 *
 * Returns: TRUE if successfull, otherwise @err is set
 **/

gboolean
c2ip_connection_send_option_request(C2IPConnection *conn, guint16 id,
				    GError **err)
{
  guint8 buffer[] = {0x00, 0x04, 0x00, 0x06, 0x06, 0x00, 0x00, 0x02, 0x01, 0x00};
  C2IP_U16_SET(&buffer[0], conn->slot);
  C2IP_U16_SET(&buffer[5], id);
  return send_request(conn, buffer, sizeof(buffer), err);
}


/**
 * c2ip_connection_send_info_request:
 * @conn: connection object
 * @id: id of the function whose extra info
 * @err: error
 *
 * Send a request for a extra info associated with @id.
 *
 * Returns: TRUE if successfull, otherwise @err is set
 **/

gboolean
c2ip_connection_send_info_request(C2IPConnection *conn, guint16 id,
				  GError **err)
{
  guint8 buffer[] = {0x00, 0x04, 0x00, 0x06, 0x06, 0x00, 0x00, 0x05, 0x00, 0x00};
  C2IP_U16_SET(&buffer[0], conn->slot);
  C2IP_U16_SET(&buffer[5], id);
  return send_request(conn, buffer, sizeof(buffer), err);
}
