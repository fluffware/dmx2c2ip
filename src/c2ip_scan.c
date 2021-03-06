#include "c2ip_scan.h"
#include "c2ip_scan_marshal.h"
#include "c2ip.h"

GQuark
c2ip_scan_error_quark()
{
  static GQuark error_quark = 0;
  if (error_quark == 0)
    error_quark = g_quark_from_static_string ("c2ip-scan-error-quark");
  return error_quark;
}

enum {
  DEVICE_FOUND,
  LAST_SIGNAL
};

static guint c2ip_scan_signals[LAST_SIGNAL] = {0 };

enum
{
  PROP_0 = 0,
  PROP_PORT,
  PROP_SCAN_INTERVAL,
  PROP_FIRST_SCAN_INTERVAL,
  N_PROPERTIES
};

struct _C2IPScan
{
  GObject parent_instance;
  GList *addresses;
  guint port;
  GSocket *socket;
  GSource *socket_source;
  guint timeout_id;
  guint scan_interval;
  guint first_scan_interval;
  guint current_scan_interval;
  GList *current_address;
};

struct _C2IPScanClass
{
  GObjectClass parent_class;

  /* class members */

  /* Signals */
  void (*device_found)(C2IPScan *scanner,
		       guint type, const gchar *name,
		       GInetAddress *addr, guint port);
};
G_DEFINE_TYPE (C2IPScan, c2ip_scan, G_TYPE_OBJECT)

static void
dispose(GObject *gobj)
{
  C2IPScan *scanner = C2IP_SCAN(gobj);
  c2ip_scan_stop(scanner);
  if (scanner->addresses) {
    g_list_free_full(scanner->addresses, g_object_unref);
    scanner->addresses = NULL;
  }
  G_OBJECT_CLASS(c2ip_scan_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  G_OBJECT_CLASS(c2ip_scan_parent_class)->finalize(gobj);
}

static void
set_property (GObject *object, guint property_id,
	      const GValue *value, GParamSpec *pspec)
{
  C2IPScan *scanner = C2IP_SCAN(object);
  switch (property_id)
    {
    case PROP_PORT:
      scanner->port = g_value_get_uint(value);
      break;
    case PROP_SCAN_INTERVAL:
      scanner->scan_interval = g_value_get_uint(value);
      break;
    case PROP_FIRST_SCAN_INTERVAL:
      scanner->first_scan_interval = g_value_get_uint(value);
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
  C2IPScan *scanner = C2IP_SCAN(object);
  switch (property_id) {
  case PROP_PORT:
    g_value_set_uint(value, scanner->port);
    break;
  case PROP_SCAN_INTERVAL:
    g_value_set_uint(value, scanner->scan_interval);
    break;
  case PROP_FIRST_SCAN_INTERVAL:
    g_value_set_uint(value, scanner->first_scan_interval);
    break;
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}
static void
device_found(C2IPScan *scanner, guint type, const gchar *name,
	      GInetAddress *addr, guint port)
{
}

static void
c2ip_scan_class_init (C2IPScanClass *klass)
{
  GParamSpec *properties[N_PROPERTIES];
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
  C2IPScanClass *scanner_class = C2IP_SCAN_CLASS(klass);
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
  gobject_class->set_property = set_property;
  gobject_class->get_property = get_property;
  scanner_class->device_found = device_found;

  properties[0] = NULL;
  properties[PROP_PORT]
    = g_param_spec_uint("name-port", "Port", "C2IP name service port",
			1, 65535, 1500,
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
   properties[PROP_SCAN_INTERVAL]
    = g_param_spec_uint("scan-interval", "Scan interval",
			"Interval in milliseconds between scanning packets, "
			"except for first scan. Set to 0 to disable scanning.",
			0, 65535, 1000,
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
    properties[PROP_FIRST_SCAN_INTERVAL]
      = g_param_spec_uint("first-scan-interval", "First scan interval",
			  "Interval in milliseconds between scanning packets "
			  "for first scan",
			  10, 65535, 100,
			  G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
   
  g_object_class_install_properties(gobject_class, N_PROPERTIES, properties);
  c2ip_scan_signals[DEVICE_FOUND] =
    g_signal_new("device-found",
		 G_OBJECT_CLASS_TYPE (gobject_class), G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(C2IPScanClass, device_found),
		 NULL, NULL,
		 c2ip_scan_marshal_VOID__UINT_STRING_OBJECT_UINT,
		 G_TYPE_NONE, 4, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INET_ADDRESS, G_TYPE_UINT);

}

static void
c2ip_scan_init(C2IPScan *scanner)
{
  scanner->port = 1500;
  scanner->addresses = NULL;
  scanner->socket = NULL;
  scanner->socket_source = NULL;
  scanner->timeout_id = 0;
  scanner->scan_interval = 1000;
  scanner->first_scan_interval = 100;
  scanner->current_address = NULL;
}



C2IPScan *
c2ip_scan_new(void)
{
  C2IPScan *scanner = g_object_new (C2IP_SCAN_TYPE, NULL);
  return scanner;
}


gboolean
handle_packet(C2IPScan *scanner, guint packet_type, guint packet_length,
	      const guint8 *buffer, GError **err)
{
  guint type;
  gchar *name;
  GInetAddress *dev_addr;
  guint dev_port;
  if (buffer[0] != 0x02) {
    g_set_error(err, C2IP_SCAN_ERROR, C2IP_SCAN_ERROR_INVALID_REPLY,
		"Not a reply");
    return FALSE;
  }
  type = buffer[1];
  name = g_strndup((gchar*)&buffer[3], buffer[2]);
  buffer += 3+ buffer[2];
  dev_addr = g_inet_address_new_from_bytes(buffer, G_SOCKET_FAMILY_IPV4);
  dev_port = C2IP_U16 (&buffer[4]);
  g_signal_emit(scanner, c2ip_scan_signals[DEVICE_FOUND], 0,
		type, name, dev_addr, dev_port);
  g_free(name);
  g_object_unref(dev_addr);
  return TRUE;
}

gboolean recv_callback(GSocket *socket, GIOCondition condition,
		       gpointer user_data)
{
  C2IPScan *scanner = user_data;
  guint8 buffer[20];
  switch(condition) {
  case G_IO_IN:
    {
      gssize size;
      GError *err = NULL;
      size = g_socket_receive_from(scanner->socket, NULL,
				   (gchar*)buffer, sizeof(buffer), NULL,
				   &err);
      if (size == -1) {
	g_warning("Failed to read reply: %s", err->message);
	g_clear_error(&err);
      }
      if (size < 4) {
	g_warning("Reply packet too short");
      } else {
	guint protocol_type = (buffer[0] << 8) | buffer[1];
	guint packet_length = (buffer[2] << 8) | buffer[3];
	if (packet_length + 4 != size) {
	  g_warning("Reply packet lengths don't match");
	} else {
	  GError *err = NULL;
	  if (!handle_packet(scanner, protocol_type, packet_length, &buffer[4], &err)) {
	    g_warning("Failed to handle reply: %s", err->message);
	    g_clear_error(&err);
	  }
	}
      }
    }
    break;
  default:
    break;
  }
  return TRUE;
}


static gboolean
timeout_callback(gpointer user_data)
{
  GInetAddress *ip_addr;
  GSocketAddress *socket_addr;
  GError *err = NULL;
  static const gchar request[] = {0x00,0x08, 0x00, 0x03, 0x01, 0x00, 0x00};
  C2IPScan *scanner = user_data;
  if (!scanner->current_address) return FALSE;
  ip_addr = scanner->current_address->data;
  socket_addr = g_inet_socket_address_new(ip_addr, scanner->port);

  if (!g_socket_send_to(scanner->socket, socket_addr, request, sizeof(request),
		       NULL, &err) == -1) {
    g_warning("Failed to send request: %s", err->message);
    g_object_unref(socket_addr);
    g_clear_error(&err);
  }
  g_object_unref(socket_addr);
  scanner->current_address = scanner->current_address->next;
  if (scanner->current_address) return TRUE;

  /* Start over */
  scanner->current_address = scanner->addresses;
  if (scanner->current_scan_interval == scanner->scan_interval) return TRUE;
  if (scanner->scan_interval > 0) {
    scanner->timeout_id = g_timeout_add(scanner->scan_interval,
					timeout_callback, scanner);
  } else {
    scanner->timeout_id = 0;
  }
  scanner->current_scan_interval = scanner->scan_interval;
  return FALSE;
}

void
c2ip_scan_stop(C2IPScan *scanner)
{
   if (scanner->timeout_id > 0) {
    g_source_remove(scanner->timeout_id);
    scanner->timeout_id = 0;
  }
  if (scanner->socket_source) {
    g_source_destroy(scanner->socket_source);
    g_source_unref(scanner->socket_source);
    scanner->socket_source = NULL;
  }
  if (scanner->socket) {
    g_socket_close(scanner->socket, NULL);
    g_clear_object(&scanner->socket);
  }
}

/**
 * Add an address to scan for devices
 *
 * @param scanner C2IPScan object
 * @param addr
 *
 */

void
c2ip_scan_add_address(C2IPScan *scanner, GInetAddress *addr)
{
  scanner->addresses = g_list_prepend(scanner->addresses, addr);
  g_object_ref(addr);
}

/**
 * Start (or restart) scanning for devices in the given range.
 *
 * @param scanner C2IPScan object
 * @param range Address range to scan.
 * May be NULL if already set by a previous call.
 *
 * @returns TRUE if the scanning started successfully
 */

gboolean
c2ip_scan_start(C2IPScan *scanner, GError **err)
{
  if (!scanner->addresses) {
    g_set_error(err, C2IP_SCAN_ERROR, C2IP_SCAN_ERROR_NO_RANGE,
		"No addresses has been set.");
    return FALSE;
  }
  c2ip_scan_stop(scanner);
  scanner->current_address = scanner->addresses;
  scanner->socket =
    g_socket_new(g_inet_address_get_family(G_INET_ADDRESS(scanner->current_address->data)),
		 G_SOCKET_TYPE_DATAGRAM,G_SOCKET_PROTOCOL_UDP,
		 err);
  if (!scanner->socket) return FALSE;
  g_socket_set_broadcast(scanner->socket, TRUE);
  scanner->socket_source =
    g_socket_create_source(scanner->socket, G_IO_IN, NULL);
  g_source_set_callback(scanner->socket_source, (GSourceFunc)recv_callback,
			scanner, NULL);
  g_source_attach(scanner->socket_source, NULL);
  scanner->timeout_id = g_timeout_add(scanner->first_scan_interval,
				      timeout_callback, scanner);
  scanner->current_scan_interval = scanner->first_scan_interval;
  return TRUE;
}

