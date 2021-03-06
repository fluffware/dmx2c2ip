#include "c2ip_connection_values.h"
#include "c2ip_connection_values_marshal.h"
#include "c2ip_function.h"
#include "c2ip.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

#define REQUEST_TIMEOUT 5

GQuark
c2ip_connection_values_error_quark()
{
  static GQuark error_quark = 0;
  if (error_quark == 0)
    error_quark =
      g_quark_from_static_string ("c2ip-connection-values-error-quark");
  return error_quark;
}

enum {
  VALUES_READY,
  VALUE_CHANGED,
  CONNECTION_CLOSED,
  LAST_SIGNAL
};

static guint c2ip_connection_values_signals[LAST_SIGNAL] = {0 };

enum
{
  PROP_0 = 0,
  N_PROPERTIES
};

enum SetupState
  {
    SETUP_NONE,
    SETUP_GETTING_VALUES,
    SETUP_GETTING_INFO,
    SETUP_DONE
  };

struct _C2IPConnectionValues
{
  GObject parent_instance;
  C2IPConnection *conn;
  guint slot;
  enum SetupState setup_state;
  C2IPDevice *device;
  GTree *values;
  GSList *pending; /* Values waiting for info */
  guint request_timeout;
};

struct _C2IPConnectionValuesClass
{
  GObjectClass parent_class;
  
  /* class members */

  /* Signals */
  void (*values_ready)(C2IPConnectionValues *values);
  void (*value_changed)(C2IPConnectionValues *values, C2IPFunction *value);
  void (*connection_closed)(C2IPConnectionValues *values);
};

G_DEFINE_TYPE (C2IPConnectionValues, c2ip_connection_values, G_TYPE_OBJECT)

static void
dispose(GObject *gobj)
{
  C2IPConnectionValues *values = C2IP_CONNECTION_VALUES(gobj);
  if (values->request_timeout > 0) {
    g_source_remove(values->request_timeout);
    values->request_timeout = 0;
  }
  if (values->pending) {
    g_slist_free(values->pending);
    values->pending = NULL;
  }
  if (values->values) {
    g_tree_destroy(values->values);
    values->values = NULL;
  }

  g_clear_object(&values->device);
  g_clear_object(&values->conn);
  G_OBJECT_CLASS(c2ip_connection_values_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  /* C2IPConnectionValues *conn = C2IP_CONNECTION_VALUES(gobj); */
  G_OBJECT_CLASS(c2ip_connection_values_parent_class)->finalize(gobj);
}

static void
set_property (GObject *object, guint property_id,
	      const GValue *value, GParamSpec *pspec)
{
  /* C2IPConnectionValues *values = C2IP_CONNECTION_VALUES(object); */
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
  /* C2IPConnectionValues *values = C2IP_CONNECTION_VALUES(object);  */
  switch (property_id) {
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
c2ip_connection_values_class_init (C2IPConnectionValuesClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
  /* C2IPConnectionValuesClass *conn_class = C2IP_CONNECTION_VALUES_CLASS(klass); */
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
  gobject_class->set_property = set_property;
  gobject_class->get_property = get_property;
  
  c2ip_connection_values_signals[CONNECTION_CLOSED] =
    g_signal_new("connection-closed",
		 G_OBJECT_CLASS_TYPE (gobject_class), G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(C2IPConnectionValuesClass, connection_closed),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__VOID,
		 G_TYPE_NONE, 0);
  
  c2ip_connection_values_signals[VALUES_READY] =
    g_signal_new("values-ready",
		 G_OBJECT_CLASS_TYPE (gobject_class), G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(C2IPConnectionValuesClass, values_ready),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__VOID,
		 G_TYPE_NONE, 0);
  c2ip_connection_values_signals[VALUE_CHANGED] =
    g_signal_new("value-changed",
		 G_OBJECT_CLASS_TYPE (gobject_class),
		 G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(C2IPConnectionValuesClass, value_changed),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__OBJECT,
		 G_TYPE_NONE, 1, C2IP_FUNCTION_TYPE);
}

static gint
id_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
  return GPOINTER_TO_SIZE(a) -  GPOINTER_TO_SIZE(b);
}

static void
c2ip_connection_values_init(C2IPConnectionValues *values)
{
  values->conn = NULL;
  values->slot = 0;
  values->setup_state = SETUP_NONE;
  values->values = g_tree_new_full(id_cmp, NULL, NULL, g_object_unref);
  values->pending = NULL;
  values->device = c2ip_device_new();
  values->request_timeout = 0;
}

static void
connected(C2IPConnection *conn, C2IPConnectionValues *values)
{
  GError *err = NULL;
#if 1
  if (!c2ip_connection_send_value_request_all(conn, &err)) {
    g_warning("Failed to request all values: %s", err->message);
    g_clear_error(&err);
  }
#endif
  values->setup_state = SETUP_GETTING_VALUES;
  g_debug("Connected");
}

static void
connection_closed(C2IPConnection *conn, C2IPConnectionValues *values)
{
  g_signal_emit(values, c2ip_connection_values_signals[CONNECTION_CLOSED], 0);
}

static gfloat
get_float16(const guint8 *b)
{
  static const float exp[] = {1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7,
			      1e-8,1e-7,1e-6, 1e-5, 1e-4, 1e-3, 1e-2,1e-1};
  uint16_t v = C2IP_U16(b);
  gfloat f = ((gfloat)((v>>4) & 0x7ff)) * exp[v&0x0f];
  if (v & 0x8000) f = -f;
  return f;
}

void
set_float16(guint8 *b, gfloat f)
{
  gboolean n = f < 0.0;
  gint m = 0;
  if (n) f = -f;
  if (f == 0.0) {
    C2IP_U16_SET(b, 0x0000);
    return;
  }
  if (f > 0x7ff) {
    do {
      f *= 0.1;
      m++;
    } while(f > 0x7ff);
    } else {
    while(f < (0x7ff / 10)) {
      f *= 10.0;
      m--;
      }
  }
  if (m > 7) {
    C2IP_U16_SET(b, 0x7ff7 & (n?0x8000:0));
  } else if (m < -8) {
    C2IP_U16_SET(b, 0x0000);
  } else {
    C2IP_U16_SET(b, (n?0x8000:0) | (lrint(f)<<4) | (m & 0x000f));
  }
}

static void 
setup_device(C2IPFunction *value)
{
  C2IPDevice *dev = c2ip_function_get_device(value);
  switch(c2ip_function_get_id(value)) {
  case C2IP_NS_CAMERA_DEVICE_ID:
    {
      const gchar *dev_id = g_value_get_string(c2ip_function_get_value(value));
      c2ip_device_set_device_id(dev, dev_id);
      c2ip_device_set_device_type(dev, C2IP_DEVICE_CAMERA_HEAD);
      
    }
    break;
  case C2IP_NS_BASE_STATION_DEVICE_ID:
    {
      const gchar *dev_id = g_value_get_string(c2ip_function_get_value(value));
      c2ip_device_set_device_id(dev, dev_id);
      c2ip_device_set_device_type(dev, C2IP_DEVICE_BASE_STATION);
    }
    break;
  case C2IP_NS_OCP_DEVICE_ID:
    {
      const gchar *dev_id = g_value_get_string(c2ip_function_get_value(value));
      c2ip_device_set_device_id(dev, dev_id);
      c2ip_device_set_device_type(dev, C2IP_DEVICE_OCP);
    }
    break;
  case C2IP_NS_SYSTEM_ALIAS:
  case C2IP_NS_CAMERA_ALIAS:
  case C2IP_NS_OCP_ALIAS:
    {
      const gchar *alias = g_value_get_string(c2ip_function_get_value(value));
      c2ip_device_set_alias(dev, alias);
    }
    break;
  }
}

static void
value_object_changed(C2IPFunction *v, GParamSpec *pspec,
		     C2IPConnectionValues *values)
{
  const GValue *gvalue;
  guint vtype;
  guint vsize;
  guint8 vbuffer[255];
  if (values->setup_state != SETUP_DONE) return;
  vtype = c2ip_function_get_value_type(v);
  gvalue = c2ip_function_get_value(v);
  switch(vtype) {
  case C2IP_TYPE_U8:
  case C2IP_TYPE_BOOL:
  case C2IP_TYPE_ENUM:
    vbuffer[0] = g_value_get_int(gvalue);
    vsize = 1;
    break;
  case C2IP_TYPE_U16:
  case C2IP_TYPE_U12:
    C2IP_U16_SET(vbuffer, g_value_get_int(gvalue));
    vsize = 2;
    break;
  case C2IP_TYPE_S16:
    C2IP_S16_SET(vbuffer, g_value_get_int(gvalue));
    vsize = 2;
    break;
  case C2IP_TYPE_FLOAT16:
    set_float16(vbuffer, g_value_get_float(gvalue));
    vsize = 2;
    break;
  case C2IP_TYPE_STRING:
    {
      const gchar *str =  g_value_get_string(gvalue);
      vsize = strlen(str);
      memcpy(vbuffer, str, vsize);
    }
  }
  {
    GError *err = NULL;
    if (!c2ip_connection_send_value_change(values->conn, c2ip_function_get_id(v),
					   vtype, vsize, vbuffer,&err)) {
      g_warning("Failed to send function value change: %s", err->message);
      g_clear_error(&err);
    }
  }
}

static void
handle_value_reply(C2IPConnectionValues *values,
		   guint length, const guint8 *packet)
{
  C2IPFunction *v;
  guint id = C2IP_U16(&packet[5]);
  guint flags = packet[7];
  guint type = packet[8] & C2IP_TYPE_MASK;
  guint value_flags = 0;
  v = g_tree_lookup(values->values, GSIZE_TO_POINTER(id));
  if (!v) {
    v = c2ip_function_new(id, type);
    c2ip_function_set_device(v, values->device);
    g_signal_connect_object(v, "notify::value",
			    (GCallback)value_object_changed ,values, 0);
    g_tree_insert(values->values, GSIZE_TO_POINTER(id), v);
  }
  g_signal_handlers_block_matched(v,  G_SIGNAL_MATCH_DATA,
				  0, 0, NULL, NULL, values);
  value_flags |= (flags & C2IP_FLAG_READ_DISABLED)? 0: C2IP_FUNCTION_FLAG_READABLE;
  value_flags |= (flags & C2IP_FLAG_WRITE_DISABLED)?0:C2IP_FUNCTION_FLAG_WRITABLE;
  value_flags |= (flags & C2IP_FLAG_HAS_INFO) ? C2IP_FUNCTION_FLAG_HAS_INFO : 0;

  c2ip_function_set_flags(v, value_flags, (C2IP_FUNCTION_FLAG_READABLE
					| C2IP_FUNCTION_FLAG_WRITABLE
					| C2IP_FUNCTION_FLAG_HAS_INFO));
  switch(type) {
  case C2IP_TYPE_U8:
  case C2IP_TYPE_BOOL:
  case C2IP_TYPE_ENUM:
    g_object_set(v, "value-int", packet[10],NULL);
    break;
  case C2IP_TYPE_S16:
    g_object_set(v, "value-int", C2IP_S16(&packet[10]),NULL);
    break;
  case C2IP_TYPE_U12:
  case C2IP_TYPE_U16:
    g_object_set(v, "value-int", C2IP_U16(&packet[10]),NULL);
    break;
  case C2IP_TYPE_STRING:
    {
      gchar *str = g_strndup((const gchar*)&packet[10], packet[9]);
      g_object_set(v, "value-string", str, NULL);
      g_free(str);
    }
    break;
  case C2IP_TYPE_FLOAT16:
    {
      gfloat f = get_float16(&packet[10]);
       g_object_set(v, "value-float", f, NULL);
    }
  }
  g_signal_handlers_unblock_matched(v,  G_SIGNAL_MATCH_DATA,
				    0, 0, NULL, NULL, values);
  setup_device(v);
  if (values->setup_state != SETUP_GETTING_VALUES) {
    g_signal_emit(values, c2ip_connection_values_signals[VALUE_CHANGED], 0, v);
  }
}
static gboolean
got_info(C2IPConnectionValues *values, guint id);

static void
handle_info_reply(C2IPConnectionValues *values,
		   guint length, const guint8 *packet)
{
  C2IPFunction *v;
  guint id = C2IP_U16(&packet[5]);
  v = g_tree_lookup(values->values, GSIZE_TO_POINTER(id));
  if (!v) return;
  switch(packet[7]) {
  case 0x03: /* Options */
    {
      guint n_options = packet[10];
      const guint8 *p = &packet[11];
      while(n_options-- > 0) {
	guint e = *p++;
	guint l = *p++;
	gchar *s = g_strndup((const gchar*)p, l);
	c2ip_function_take_option(v, e, s);
	p += l;
      }
      got_info(values,id);
    }
    break;
  case 0x06: /* Extra */
    {
      guint l = packet[13];
      gchar *s = g_strndup((const gchar*)&packet[14], l);
      c2ip_function_set_unit(v, s);
      g_free(s);
      got_info(values,id);
    }
    break;
  }
}

static gboolean
request_next_info(C2IPConnectionValues *values);

static gboolean
request_timed_out(gpointer user_data)
{
  C2IPConnectionValues *values = user_data;
  g_debug("Request timed out");
  if (values->pending) {
    values->pending = g_slist_delete_link(values->pending, values->pending);
    request_next_info(values);
  }
  return FALSE;
}

static gboolean
request_next_info(C2IPConnectionValues *values)
{
  C2IPFunction *v;
  GError *err = 0;
  
  if (values->request_timeout > 0) {
    g_source_remove(values->request_timeout);
  }
  values->request_timeout =
    g_timeout_add_seconds(REQUEST_TIMEOUT, request_timed_out, values);
  while(values->pending) {
    guint id;
    guint type;
    guint flags;
    v = values->pending->data;
    id = c2ip_function_get_id(v);
    type = c2ip_function_get_value_type(v);
    flags = c2ip_function_get_flags(v);
    if (type == C2IP_TYPE_BOOL || type == C2IP_TYPE_ENUM) {
      if (!c2ip_connection_send_option_request(values->conn, id, &err)) {
	g_warning("Failed to request options: %s", err->message);
	g_clear_error(&err);
      } else return TRUE;
    } else if (flags & C2IP_FUNCTION_FLAG_HAS_INFO) {
      if (!c2ip_connection_send_info_request(values->conn, id, &err)) {
	g_warning("Failed to request extra info: %s", err->message);
	g_clear_error(&err);
      } else return TRUE;
    } else {
      values->pending = g_slist_delete_link(values->pending, values->pending);
    }
  }
  values->setup_state = SETUP_DONE;
  g_signal_emit(values, c2ip_connection_values_signals[VALUES_READY], 0);
  return FALSE;
}

static gboolean
got_info(C2IPConnectionValues *values, guint id)
{
  C2IPFunction *v;
  if (values->setup_state != SETUP_GETTING_INFO) return FALSE;
  g_assert (values->pending);
  v = values->pending->data;
  if (c2ip_function_get_id(v) == id) {
    values->pending = g_slist_delete_link(values->pending, values->pending);
    return request_next_info(values);
  }
  return TRUE;
}
  
static gboolean
prepend_value(gpointer key,
	      gpointer value,
	      gpointer data)
{
  GSList **list = data;
  g_assert(IS_C2IP_FUNCTION(value));
  *list = g_slist_prepend(*list, value);
  return FALSE;
}

static void
received_packet(C2IPConnection *conn, guint length, const guint8 *packet,
		C2IPConnectionValues *values)
{
  guint packet_type = C2IP_U16(&packet[0]);
  guint dlen = C2IP_U16(&packet[2]);
  if (dlen +4 != length) {
    g_warning("Inconsistent packet length");
    return;
  }
  if (packet_type == values->slot) {
    switch(packet[4]) {
    case C2IP_REPLY_VALUE:
    case C2IP_INDICATION_VALUE:
      handle_value_reply(values, length, packet);
      break;
    case C2IP_REPLY_VALUE_ALL_DONE:
      values->setup_state = SETUP_GETTING_INFO;
      g_tree_foreach(values->values, prepend_value, &values->pending);
      request_next_info(values);
      break;
    case C2IP_REPLY_FUNC_INFO:
      handle_info_reply(values, length, packet);
      break;
    }
  }
}



C2IPConnectionValues *
c2ip_connection_values_new(C2IPConnection *conn)
{
  C2IPConnectionValues *values =
    g_object_new (C2IP_CONNECTION_VALUES_TYPE, NULL);
  values->conn = conn;
  g_object_get(conn, "slot", &values->slot, NULL);
  g_object_ref(conn);
  g_signal_connect_object(conn, "connected", (GCallback)connected, values,0);
  g_signal_connect_object(conn, "connection-closed", (GCallback)connection_closed, values,0);
  g_signal_connect_object(conn, "received-packet", (GCallback)received_packet, values,0);
  return values;
}

C2IPFunction *
c2ip_connection_values_get_function(const C2IPConnectionValues *values,
				    guint id)
{
  return C2IP_FUNCTION(g_tree_lookup(values->values, GSIZE_TO_POINTER(id)));
}

/**
 * c2ip_connection_values_for_each
 * @values:
 * @id: function ID
 *
 **/

struct ValueCB
{
  C2IPFunctionCallback cb;
  gpointer data;
};
static gboolean
value_iterate(gpointer key, gpointer value, gpointer data)
{
  struct ValueCB *c = data;
  return c->cb(C2IP_FUNCTION(value), c->data);
}

void
c2ip_connection_values_foreach(C2IPConnectionValues *values,
				C2IPFunctionCallback cb, gpointer user_data)
{
  struct ValueCB c;
  c.cb = cb;
  c.data = user_data;
  g_tree_foreach(values->values, value_iterate, &c);
}

C2IPDevice *
c2ip_connection_values_get_device(C2IPConnectionValues *values)
{
  return values->device;
}
