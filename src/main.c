#include <stdio.h>
#include <serial_dmx_recv.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <glib-unix.h>
#include "httpd.h"
#include <c2ip.h>
#include <c2ip_strings.h>
#include <c2ip_scan.h>
#include <c2ip_connection_manager.h>
#include <c2ip_connection_values.h>
#include <c2ip_device.h>
#include <dmx_c2ip_mapper.h>
#include <json-glib/json-glib.h>
#include <json-glib/json-glib.h>
#include <syslog.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef WITH_OLA
#include <ola_dmx_recv.h>
#endif

static gboolean
sigint_handler(gpointer user_data)
{
  g_main_loop_quit(user_data);
  return TRUE;
}

typedef struct AppContext AppContext;
struct AppContext
{
  char *config_filename;
  GKeyFile *config_file;
  gboolean daemon;
  GLogLevelFlags log_level_mask;
  char *pid_filename;
  gchar *dmx_device;
  int dmx_speed;
  guint ola_universe;
  DMXRecv *dmx_recv;
  HTTPServer *http_server;
  C2IPScan *c2ip_scanner;
  C2IPConnectionManager *c2ip_connection_manager;
  DMXC2IPMapper *mapper;
  sqlite3 *db;
};
  
AppContext app_ctxt  = {
  NULL,
  NULL,
  FALSE,
  ~G_LOG_LEVEL_DEBUG,
  NULL,
  NULL,
  250000,
  0,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL,
  NULL
};


static GQuark values_quark;

static void
app_init(AppContext *app)
{
  values_quark = g_quark_from_static_string("c2ip-connection-values");
}

static void
app_cleanup(AppContext* app)
{
  g_clear_object(&app->mapper);
  if (app->db) {
    sqlite3_close(app->db);
    app->db = NULL;
  }
  g_clear_object(&app->c2ip_scanner);
  g_clear_object(&app->c2ip_connection_manager);
  g_clear_object(&app->dmx_recv);
  g_clear_object(&app->http_server);
  if (app->config_filename) g_key_file_unref(app->config_file);
  g_free(app->config_filename);
  g_free(app->dmx_device);
  if (app->pid_filename) {
    remove(app->pid_filename);
    g_free(app->pid_filename);
  }
  if (app->daemon) {
    closelog();
  }
}

static const gchar *
device_type_to_string(guint type)
{
  GEnumValue *v;
  v = g_enum_get_value(C2IP_DEVICE_TYPE_ENUM_CLASS, type);
  if (!v) return "unknown";
  return v->value_name;
}

static guint
string_to_device_type(const gchar *str)
{
  GEnumValue *v =
    g_enum_get_value_by_name(g_type_class_peek(C2IP_DEVICE_TYPE_ENUM_TYPE),str);
  if (!v) return 0;
  return v->value;
}


static void
append_device_path(GString *str, const C2IPDevice *dev)
{
  g_string_append(str,  c2ip_device_get_device_name(dev));
  g_string_append_c(str, '/');
  g_string_append(str, device_type_to_string(c2ip_device_get_device_type(dev)));
}

static void
configure_string_property(void *obj, const gchar *property, GKeyFile *config,
			  const gchar *group, const gchar *key)
{
  GError *err = NULL;
  guchar *str;
  str = (guchar*)g_key_file_get_string(config, group, key, &err);
  if (str) {
    g_object_set(obj, property, str, NULL);
  } else {
    g_clear_error(&err);
  }
  g_free(str);
}

static gboolean
parse_device_path(gchar **p, guint *type, gchar **namep)
{
  gchar *name = *p;
  gchar *typestr;
  gchar *end;
  typestr = index(name, '/');
  if (typestr == NULL || typestr == name) return FALSE;
  *typestr++ = '\0';
  end = index(typestr, '/');
  if (end == NULL || typestr == end) return FALSE;
  *end++ = '\0';
  *type = string_to_device_type(typestr);
  if (!*type) return FALSE;
  *namep = name;
  *p = end;
  return TRUE;
}

static C2IPConnectionValues *
get_values(AppContext *app, guint type, const gchar *name)
{
  C2IPConnection *conn;
  conn = c2ip_connection_manager_get_connection(app->c2ip_connection_manager,
						type, name);
  if (!conn) {
    g_warning("No matching device found");
    return NULL;
  }
  return C2IP_CONNECTION_VALUES(g_object_get_qdata(G_OBJECT(conn),
						   values_quark));
}
  
static void
change_c2ip_function(AppContext *app, const gchar *pathstr, const GValue *gvalue)
{
  GValue transformed = G_VALUE_INIT;
  C2IPConnectionValues *values;
  C2IPFunction *v;
  guint type;
  gchar *name;
  gchar *p = g_alloca(strlen(pathstr));
  strcpy(p, pathstr);
  if (!parse_device_path(&p, &type, &name)) return;

  values = get_values(app, type, name);
  if (!values) return;
  v = c2ip_connection_values_get_function(values, atoi(p));
  if (!v) {
    g_warning("No matching ID");
    return;
  }
  g_value_init(&transformed, G_VALUE_TYPE(c2ip_function_get_value(v)));
  if (!g_value_transform(gvalue, &transformed)) {
    g_value_unset(&transformed);
    return;
  }
  c2ip_function_set_value(v, &transformed);
  g_value_unset(&transformed);
  g_debug("Changing value %d %s %s", type, name, p);
}

gfloat
get_http_float_device(HTTPServer *server,
		      const gchar *prefix, const gchar *suffix,
		      const C2IPDevice *device, guint id, GError **err)
{
  double v;
  GString *str;
  str = g_string_new(prefix);
  append_device_path(str, device);
  g_string_append_printf(str, "/%d/", id);
  g_string_append(str, suffix);
  http_server_get_double(server, str->str, &v, err);
  g_string_free(str, TRUE);
  return v;
}

gint
g_value_get_as_int(const GValue *gvalue)
{
  GValue t = G_VALUE_INIT;
  gint v;
  g_value_init(&t, G_TYPE_INT);
  if (!g_value_transform(gvalue, &t)) {
    g_critical("Value can't be transformed to int");
    return 0;
  }
  v = g_value_get_int(&t);
  g_value_unset(&t);
  return v;
}

static void
change_mapping(AppContext *app, const gchar *pathstr, const GValue *gvalue)
{
  guint id;
  gchar *attr;
  guint type;
  gchar *name;
  gchar *p = g_alloca(strlen(pathstr));
  strcpy(p, pathstr);
  if (!parse_device_path(&p, &type, &name)) return;
  id = strtoul(p, &attr, 10);
  if (p == attr || *attr != '/') return;
  attr++;
 
  
  if (strcmp(attr, "channel") == 0) {
    GError *err;
    guint channel = g_value_get_int64(gvalue);
    gfloat min = 0;
    gfloat max = 0;
    
    if (channel > 512) return;
    if (channel > 0) {
      C2IPFunction *func;
      C2IPConnectionValues *values;
      values = get_values(app, type, name);
      if (!values) return;
      func = c2ip_connection_values_get_function(values, id);
      if (!func) {
	g_warning("No matching function found for mapping");
	return;
      }
      min = get_http_float_device(app->http_server, "dmxmap/", "min",
				  c2ip_function_get_device(func), id, NULL);
      max = get_http_float_device(app->http_server, "dmxmap/","max",
				  c2ip_function_get_device(func), id, NULL);
      if (!dmx_c2ip_mapper_add_map_function(app->mapper,
					    channel, func, min, max, &err)) {
	g_warning("Failed to set mapping: %s", err->message);
	g_clear_error(&err);
	return;
      }
    } else {
      if (!dmx_c2ip_mapper_remove_func(app->mapper, type, name, id)) {
	g_warning("Failed to remove mapping");
	return;
      }
    }
  } else if (strcmp(attr, "min") == 0) {
    gfloat min = g_value_get_double(gvalue);
    dmx_c2ip_mapper_set_min(app->mapper, type, name, id, min);
  } else  if (strcmp(attr, "max") == 0) {
    gfloat max = g_value_get_double(gvalue);
    dmx_c2ip_mapper_set_max(app->mapper, type, name, id, max);
  } else{
    g_warning("Unknown mapping attribute '%s'", attr);
  }
}

static void
http_value_changed(HTTPServer *server, GQuark path, const GValue *value,
		   AppContext *app)
{
  const gchar *pathstr = g_quark_to_string(path);
#if 0
  gchar *vstr = g_strdup_value_contents(value);
  g_debug("%s = %s", g_quark_to_string(path),vstr);
  g_free(vstr);
#endif
  if (g_str_has_prefix(pathstr, "functions/values/")) {
    change_c2ip_function(app, &pathstr[17], value);
  } else if (g_str_has_prefix(pathstr, "dmxmap/")) {
    change_mapping(app, &pathstr[7], value);
  } else {
    g_warning("Unhandled value from HTTP server: %s", pathstr);
  }
}

static void
configure_http_server(AppContext *app)
{
  GError *err = NULL;
  guint port;
  /* Break update loops */
  g_object_set(app->http_server, "http-sets-values", FALSE,
	       "user-changes-signaled", FALSE, NULL);
  port = g_key_file_get_integer(app_ctxt.config_file, "HTTP", "Port", &err);
  if (!err) {
    g_object_set(app->http_server, "http-port", port, NULL);
  } else {
    g_clear_error(&err);
  }
  configure_string_property(app->http_server, "user",
			    app_ctxt.config_file, "HTTP", "User");
  configure_string_property(app->http_server, "password",
			    app_ctxt.config_file, "HTTP", "Password");
  configure_string_property(app->http_server, "http-root",
			    app_ctxt.config_file, "HTTP", "Root");
  configure_string_property(app->http_server, "root-file",
			    app_ctxt.config_file, "HTTP", "RootFile");
  
  g_signal_connect(app->http_server, "value-changed", (GCallback)http_value_changed, app);
}


static void
dmx_packet(DMXRecv *recv, guint l, guint8 *packet, AppContext *app)
{
  gchar buffer[30];
  guint i;
  for (i = 0; i < l;i++) {
    if (dmx_recv_channels_changed(recv, i, i + 1)) {
      g_snprintf(buffer, sizeof(buffer), "dmx_channels/%d", i+1);
      http_server_set_int(app->http_server, buffer, packet[i], NULL);
      if (app->mapper) {
	dmx_c2ip_mapper_set_channel(app->mapper, i + 1, packet[i], NULL);
      }
    }
  }
}

static void
device_found(C2IPScan *scanner,
	     guint type, const gchar *name,
	     GInetAddress *addr, guint port, AppContext *app)
{
  gchar *addr_str = g_inet_address_to_string(addr);
  /* g_debug("Found type %d, name '%s' @ %s:%d", type, name, addr_str, port); */
  g_free(addr_str);
}

static gboolean
setup_c2ip_scanner(AppContext *app, GError **err)
{
  gchar **addrs_str;
  app->c2ip_scanner = c2ip_scan_new();
  addrs_str = g_key_file_get_string_list(app->config_file, "C2IP", "Addresses",
					 NULL, err);
  if (!addrs_str) {
    GInetAddress *addr;
    g_clear_error(err);
    g_warning("No address given. Connecting to localhost");
    addr = g_inet_address_new_loopback(G_SOCKET_FAMILY_IPV4);
    c2ip_scan_add_address(app->c2ip_scanner, addr);
    g_object_unref(addr);
  } else {
    GInetAddress *addr;
    gchar **str = addrs_str;
    while(*str) {
      addr = g_inet_address_new_from_string(*str);
      if (addr) {
	c2ip_scan_add_address(app->c2ip_scanner, addr);
	g_object_unref(addr);
      } else {
	g_warning("Invalid address %s", *str);
      }
      str++;
    }
    g_strfreev (addrs_str);
  }
  g_object_set(app->c2ip_scanner,
	       "first-scan-interval", 1000,
	       "scan-interval", 5000, NULL);
  g_signal_connect(app->c2ip_scanner, "device-found",
		   (GCallback)device_found, app);
  if (!c2ip_scan_start(app->c2ip_scanner, err)) {
    return FALSE;
  }
  return TRUE;
}


static void
connection_closed(C2IPConnectionValues *values, AppContext *app)
{
  GString *path = g_string_new("functions/values/");
  const C2IPDevice *dev = c2ip_connection_values_get_device(values);
  append_device_path(path, dev);
  http_server_remove(app->http_server, path->str, NULL);
  g_string_assign(path, "functions/attributes/");
  append_device_path(path, dev);
  http_server_remove(app->http_server, path->str, NULL);
  g_string_free(path,TRUE);
  g_object_unref(values);
  g_debug("Connection closed");
}

#if 0
static gboolean
print_value(C2IPValue *value, gpointer user_data)
{
  gchar *str;
  C2IPDevice *dev = c2ip_function_get_device(value);
  switch(c2ip_device_get_device_type(dev)) {
  case C2IP_DEVICE_CAMERA_HEAD:
    fputs("Camera ",stdout);
    break;
  case C2IP_DEVICE_BASE_STATION:
    fputs("Base ", stdout);
    break;
  case C2IP_DEVICE_OCP:
    fputs("OCP ", stdout);
    break;
  }
  
  fprintf(stdout, "%s \"%s\" ", c2ip_device_get_device_name(dev),
	  c2ip_string_map_default(c2ip_funtion_name_map,
				  c2ip_funtion_name_map_length,
				  c2ip_function_get_id(value),
				  "?"));
  str = c2ip_function_to_string(value);
  fputs(str,stdout);
  fputc('\n', stdout);
  fflush(stdout);
  g_free(str);
  return FALSE;
}
#endif

struct ExportOptions
{
  GString *path;
  gsize prefix_len;
  HTTPServer *server;
  C2IPFunction *value;
};

static gboolean
export_option(guint n, const gchar *name,
	      gpointer user_data)
{
  struct ExportOptions *export = user_data;
  g_string_append_printf(export->path,"%d",n);
  http_server_set_string(export->server, export->path->str,
			 name, NULL);
  g_string_truncate( export->path, export->prefix_len);
  return FALSE;
}

static void
export_options(C2IPFunction *value, GString *path, AppContext *app)
{
  gssize prefix_len = path->len;
  struct ExportOptions export;
  g_string_append(path, "options/");
  export.prefix_len = path->len;
  export.path = path;
  export.server = app->http_server;
  export.value = value;
  c2ip_function_options_foreach(value, export_option, &export);
  g_string_truncate(path, prefix_len);
}

gboolean
get_mapping(DMXC2IPMapper *mapper,
	    C2IPFunction *func, guint *channel, gfloat *min, gfloat *max)
{
  C2IPDevice *dev = c2ip_function_get_device(func);
  return dmx_c2ip_mapper_get_function_mapping(mapper,
					      c2ip_device_get_device_type(dev),
					      c2ip_device_get_device_name(dev),
					      c2ip_function_get_id(func),
					      channel, min, max);
}

static void
export_mapping(AppContext *app, C2IPFunction *value)
{
  guint vtype = c2ip_function_get_value_type(value);
  gfloat min;
  gfloat max;
  guint channel = 0;
  gboolean map = TRUE;

  /* Check if there already is a mapping, if not make up some default
     values based on the type */
  if (!(app->mapper && get_mapping(app->mapper, value, &channel, &min, &max))) {
    switch(vtype) {
    case C2IP_TYPE_U8:
      min = 0.0;
      max = 255.0;
      break;
    case C2IP_TYPE_U12:
      min = 0.0;
      max = 4095;
      break;
    case C2IP_TYPE_U16:
      min = 0.0;
      max = 65535;
      break;
    case C2IP_TYPE_S16:
      min = -32768.0;
      max = 32767;
      break;
    case C2IP_TYPE_FLOAT16:
      min = 0.0;
      max = 1.0;
      break;
    default:
      map = FALSE;
    }
  }
  if (map) {
    guint prefix_len;
    C2IPDevice *dev = c2ip_function_get_device(value);
    GString *str = g_string_new("dmxmap/");
    append_device_path(str, dev);
    g_string_append_printf(str, "/%d/", c2ip_function_get_id(value));
    prefix_len = str->len;
    g_string_append(str, "channel");
    http_server_set_int(app->http_server, str->str, channel, NULL);
    g_string_truncate(str, prefix_len);
    g_string_append(str, "min");
    http_server_set_double(app->http_server, str->str, min, NULL);
    g_string_truncate(str, prefix_len);
    g_string_append(str, "max");
    http_server_set_double(app->http_server, str->str, max, NULL);
    g_string_free(str, TRUE);
  }
}

/* Export a value too the world through the web server */
static void
export_function(AppContext *app, C2IPFunction *value)
{
  gsize prefix_len;
  GString *str = g_string_new("functions/values/");
  C2IPDevice *dev = c2ip_function_get_device(value);
  guint vtype = c2ip_function_get_value_type(value);
  append_device_path(str, dev);
  g_string_append_printf(str, "/%d", c2ip_function_get_id(value));
  switch(vtype) {
  case C2IP_TYPE_U8:
  case C2IP_TYPE_U12:
  case C2IP_TYPE_U16:
  case C2IP_TYPE_S16:
  case C2IP_TYPE_ENUM:
  case C2IP_TYPE_BOOL:
    http_server_set_int(app->http_server, str->str,
			g_value_get_int(c2ip_function_get_value(value)) , NULL);
    break;
  case C2IP_TYPE_STRING:
    http_server_set_string(app->http_server, str->str,
			   g_value_get_string(c2ip_function_get_value(value)) ,
			   NULL);
    break;
  case C2IP_TYPE_FLOAT16:
    http_server_set_double(app->http_server, str->str,
			   g_value_get_float(c2ip_function_get_value(value)) ,
			   NULL);
    break;
  }
  g_string_assign(str, "functions/attributes/");
  append_device_path(str, dev);
  g_string_append_printf(str, "/%d/", c2ip_function_get_id(value));
  prefix_len = str->len;
  g_string_append(str, "type");
  http_server_set_string(app->http_server, str->str,
			 c2ip_function_get_value_type_string(value), NULL);
  g_string_truncate(str, prefix_len);
  g_string_append(str, "name");
  http_server_set_string(app->http_server, str->str,
			 c2ip_function_get_name(value), NULL);
  g_string_truncate(str, prefix_len);
  
  if (c2ip_function_get_unit(value)) {
    g_string_append(str, "unit");
    http_server_set_string(app->http_server, str->str,
			   c2ip_function_get_unit(value), NULL);
    g_string_truncate(str, prefix_len);
  }
  export_options(value, str, app);
  
 
  g_string_free(str, TRUE);
  
}

static void
bind_function(AppContext *app, C2IPFunction *func)
{
  dmx_c2ip_mapper_bind_function(app->mapper, func, NULL);
}

static gboolean
new_function_available(C2IPFunction *func, gpointer user_data)
{
  AppContext *app = user_data;
  export_function(app, func);
  export_mapping(app, func);
  bind_function(app, func);
  return FALSE;
}

static void
values_ready(C2IPConnectionValues *values, AppContext *app)
{
  c2ip_connection_values_foreach(values, new_function_available, app);
  g_debug("Values ready");
}

static void
c2ip_function_changed(C2IPConnectionValues *values, C2IPFunction *func,
		      AppContext *app)
{
  export_function(app, func);
}


static void
new_connection(C2IPConnectionManager *cm, C2IPConnection *conn, guint device_type, const gchar *name, guint slot, AppContext *app)
{
  C2IPDevice *dev;
  C2IPConnectionValues *values = c2ip_connection_values_new(conn);
  dev = c2ip_connection_values_get_device(values);
  c2ip_device_set_device_type(dev, device_type);
  c2ip_device_set_device_name(dev, name);
  g_object_set_qdata(G_OBJECT(conn), values_quark, values);
  g_object_ref(values);
  g_signal_connect(values, "connection-closed",
		   (GCallback)connection_closed, app);
   g_signal_connect(values, "values-ready",
		   (GCallback)values_ready, app);
   g_signal_connect(values, "value-changed",
		   (GCallback)c2ip_function_changed, app);
  
  g_debug("New connection");
}

static gboolean
setup_c2ip(AppContext *app, GError **err)
{
  if (!setup_c2ip_scanner(app, err)) return FALSE;
  app->c2ip_connection_manager = c2ip_connection_manager_new();
  g_signal_connect_object(app->c2ip_scanner, "device-found",
			  (GCallback)c2ip_connection_manager_add_device,
			  app->c2ip_connection_manager, G_CONNECT_SWAPPED);
  g_signal_connect(app->c2ip_connection_manager, "new-connection",
		   (GCallback)new_connection, app);
  return TRUE;
}

static gboolean
setup_db(AppContext *app, GError **err)
{
  int rc;
  gchar *db_filename;
  gchar *table;
  db_filename = g_key_file_get_string(app->config_file, "DB", "File", err);
  if (!db_filename) {
    return FALSE;
  }
  table = g_key_file_get_string(app->config_file, "DB", "MapTable", err);
  if (!table) {
    g_clear_error(err);
    table = g_strdup("dmx_c2ip_map");
  }

  rc = sqlite3_open(db_filename, &app->db);
  g_free(db_filename);
  if (rc) {
    g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
		"Failed to open database: %s",sqlite3_errmsg(app->db));
    sqlite3_close(app->db);
    g_free(table);
    return FALSE;
  } 
  g_free(table);
  return TRUE;
}

static void
dmx_mapping_changed(DMXC2IPMapper *mapper, guint channel, guint dev_type,
		    const gchar *dev_name, guint func_id, AppContext *app)
{
  if (app->http_server) {
    gfloat min, max;
    GString *path = g_string_new("dmxmap/");
    guint prefix_len;
    g_string_append_printf(path,"%s/%s/%d/",
			   dev_name, device_type_to_string(dev_type), func_id);
    prefix_len = path->len;
    g_string_append(path,"channel");
    http_server_set_int(app->http_server, path->str, channel, NULL);

    if (dmx_c2ip_mapper_get_function_mapping(mapper, dev_type, dev_name,
					     func_id,
					     &channel, &min, &max)) {
      g_string_truncate(path, prefix_len);
      g_string_append(path,"min");
      http_server_set_double(app->http_server, path->str, min, NULL);

      g_string_truncate(path, prefix_len);
      g_string_append(path,"max");
      http_server_set_double(app->http_server, path->str, max, NULL);
    }
    g_string_free(path, TRUE);
  }
}

static void
dmx_mapping_removed(DMXC2IPMapper *mapper, guint channel, guint dev_type,
		    const gchar *dev_name, guint func_id, AppContext *app)
{
  if (app->http_server) {
    GString *path = g_string_new("dmxmap/");
    g_string_append_printf(path,"%s/%s/%d/",
			   dev_name, device_type_to_string(dev_type), func_id);
    g_string_append(path,"channel");
    http_server_set_int(app->http_server, path->str, 0, NULL);
    g_string_free(path, TRUE);
  }
}

static gboolean
setup_mapper(AppContext *app, GError **err)
{
  app->mapper = dmx_c2ip_mapper_new();
  if (!app->mapper) return FALSE;
  g_signal_connect(app->mapper, "mapping-changed",
		   (GCallback)dmx_mapping_changed, app);
  g_signal_connect(app->mapper, "mapping-removed",
		   (GCallback)dmx_mapping_removed, app);
  return TRUE;
}

#define SYSLOG_IDENTITY "dmx2c2ip"

static void
syslog_handler(const gchar *log_domain, GLogLevelFlags log_level,
	      const gchar *message, gpointer user_data)
{
  AppContext *app = user_data;
  int pri = LOG_INFO;
  if ((log_level & app->log_level_mask) != log_level) return;
  switch(log_level)
    {
    case G_LOG_LEVEL_ERROR:
      pri = LOG_CRIT;
      break;
    case G_LOG_LEVEL_CRITICAL:
      pri = LOG_ERR;
      break;
    case G_LOG_LEVEL_WARNING:
      pri = LOG_WARNING;
      break;
    case G_LOG_LEVEL_MESSAGE:
      pri = LOG_NOTICE;
      break;
    case G_LOG_LEVEL_INFO:
      pri = LOG_INFO;
      break;
    case G_LOG_LEVEL_DEBUG:
      pri = LOG_DEBUG;
      break;
    default:
      break;
    }
  syslog(pri, "%s", message);
}

static gboolean
go_daemon(AppContext *app, GError **err)
{
  pid_t sid;
  pid_t pid;
  int pid_file;
  openlog(SYSLOG_IDENTITY, LOG_NDELAY | LOG_PID, LOG_USER);
  g_log_set_default_handler(syslog_handler, app);
  g_message("Logging started");
  
  pid = fork();
  if (pid < 0) {
    g_set_error(err, G_FILE_ERROR, g_file_error_from_errno(errno),
		"fork failed: %s", strerror(errno));
    return FALSE;
  }
  
  umask(S_IWGRP | S_IWOTH);

  if (pid > 0) {
    if (app->pid_filename) {
      pid_file = open(app->pid_filename, O_WRONLY | O_CREAT | O_TRUNC,
		      S_IRUSR | S_IWUSR | S_IRGRP|S_IROTH);
      if (pid_file >= 0) {
	char buffer[10];
	snprintf(buffer, sizeof(buffer), "%d\n", pid);
	if (write(pid_file, buffer, strlen(buffer)) <= 0) {
	  g_critical("Failed to write pid file\n");
	}
	close(pid_file);
      } else {
	g_critical("Failed to open pid file: %s\n", strerror(errno));
      }
    }
    exit(EXIT_SUCCESS);
  }
  

  sid = setsid();
  if (sid < 0) {
    syslog(LOG_ERR, "Could not create process group\n");
    exit(EXIT_FAILURE);
  }
  
  if ((chdir("/")) < 0) {
    syslog(LOG_ERR, "Could not change working directory to /\n");
    exit(EXIT_FAILURE);
  }

  
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);
  g_message("Daemon detached");
  
 
  return TRUE;
}


const GOptionEntry app_options[] = {
  {"config-file", 'c', 0, G_OPTION_ARG_FILENAME,
   &app_ctxt.config_filename, "Configuration file", "FILE"},
  {"daemon", 'd', 0, G_OPTION_ARG_NONE,
   &app_ctxt.daemon, "Detach and use syslog", NULL},
  {"pid-file", 'p', 0, G_OPTION_ARG_FILENAME,
   &app_ctxt.pid_filename, "Create a pid file when running as daemon",
   "FILE"},
  {NULL}
};

int
main(int argc, char *argv[])
{
  GError *err = NULL;
  GOptionContext *opt_ctxt;
  GMainLoop *loop;
#ifdef MEMPROFILE
  g_mem_set_vtable (glib_mem_profiler_table);
#endif
  app_init(&app_ctxt);
  g_type_init();
  opt_ctxt = g_option_context_new (" - map DMX to C2IP");
  g_option_context_add_main_entries(opt_ctxt, app_options, NULL);
  if (!g_option_context_parse(opt_ctxt, &argc, &argv, &err)) {
    g_printerr("Failed to parse options: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  g_option_context_free(opt_ctxt);
  if (!app_ctxt.config_filename) {
    g_printerr("No configuration file\n");
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  if (app_ctxt.config_filename) {
    app_ctxt.config_file = g_key_file_new();
    g_key_file_set_list_separator(app_ctxt.config_file, ',');
    if (!g_key_file_load_from_file(app_ctxt.config_file,
				   app_ctxt.config_filename,
				   G_KEY_FILE_NONE, &err)) {
      g_printerr("Failed to read configuration file: %s\n", err->message);
      app_cleanup(&app_ctxt);
      return EXIT_FAILURE;
    }
  }
  if (app_ctxt.daemon) {
    if (!go_daemon(&app_ctxt, &err)) {
      g_printerr("Failed to start as daemon: %s\n", err->message);
      g_clear_error(&err);
      app_cleanup(&app_ctxt);
      return EXIT_FAILURE;
    }
  }

  if (!setup_db(&app_ctxt, &err)) {
    g_critical("Failed to setup db: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  
  if (!setup_mapper(&app_ctxt, &err)) {
    g_critical("Failed to setup DMX mapper: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  app_ctxt.dmx_device =
    g_key_file_get_string(app_ctxt.config_file, "DMXPort", "Device", &err);
  if (!app_ctxt.dmx_device) {
    if (!g_error_matches(err, G_KEY_FILE_ERROR,
			 G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
      g_warning("Failed to parse DMXPort: %s", err->message);
    }
    g_clear_error(&err);
  }
  app_ctxt.ola_universe =
    g_key_file_get_integer(app_ctxt.config_file, "DMXPort", "OLAUniverse",
			   &err);
  if (!app_ctxt.ola_universe) {
    if (!g_error_matches(err, G_KEY_FILE_ERROR,
			 G_KEY_FILE_ERROR_KEY_NOT_FOUND)) {
      g_warning("Failed to parse OLAUniverse: %s", err->message);
    }
    g_clear_error(&err);
  }
  if (!app_ctxt.dmx_device && ! app_ctxt.ola_universe) {
    g_warning("No port or universe. DMX disabled");
  } else {
    g_assert(err == NULL);
    if (app_ctxt.dmx_device) {
      app_ctxt.dmx_speed =
	g_key_file_get_integer(app_ctxt.config_file, "DMXPort", "Speed", &err);
      if (err) {
	g_message("Using default speed 250kbps");
	app_ctxt.dmx_speed = 250000;
	g_clear_error(&err);
      }
      app_ctxt.dmx_recv = serial_dmx_recv_new(app_ctxt.dmx_device, &err);
    } else {
#ifdef WITH_OLA
      app_ctxt.dmx_recv = ola_dmx_recv_new(app_ctxt.ola_universe, &err);
#else
      g_critical("Program not built with OLA support");
      app_cleanup(&app_ctxt);
      return EXIT_FAILURE;
#endif
    }
    if (!app_ctxt.dmx_recv) {
      g_critical("Failed setup DMX port: %s", err->message);
      g_clear_error(&err);
      app_cleanup(&app_ctxt);
      return EXIT_FAILURE;
    }
    g_signal_connect(app_ctxt.dmx_recv, "new-packet", (GCallback)dmx_packet, &app_ctxt);
  }
  
  app_ctxt.http_server = http_server_new();
  configure_http_server(&app_ctxt);
  if (!http_server_start(app_ctxt.http_server, &err)) {
    g_critical("Failed to setup HTTP server: %s\n", err->message);
    g_clear_object(&app_ctxt.http_server);
    g_clear_error(&err);
    return EXIT_FAILURE;
  }
  
  if (!setup_c2ip(&app_ctxt, &err)) {
    g_critical("Failed to set up C2IP: %s\n", err->message);
    g_clear_error(&err);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }

  if (!dmx_c2ip_mapper_read_db(app_ctxt.mapper,
			       app_ctxt.db, "dmx_c2ip_map", &err)) {
    g_critical("Failed reading mappings: %s\n", err->message);
    g_clear_error(&err);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  loop = g_main_loop_new(NULL, FALSE);
  g_unix_signal_add(SIGINT, sigint_handler, loop);
  g_unix_signal_add(SIGTERM, sigint_handler, loop);
  g_debug("Starting");
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
  g_message("Exiting");
  app_cleanup(&app_ctxt);
#ifdef MEMPROFILE
  g_mem_profile ();
#endif
  return EXIT_SUCCESS;
}
