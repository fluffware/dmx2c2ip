#include <stdio.h>
#include <dmx_recv.h>
#include <stdlib.h>
#include <string.h>
#include <glib-unix.h>
#include "httpd.h"
#include <c2ip.h>
#include <c2ip_strings.h>
#include <c2ip_scan.h>
#include <c2ip_connection_manager.h>
#include <c2ip_connection_values.h>
#include <dmx_c2ip_mapper.h>
#include <json-glib/json-glib.h>
#include <json-glib/json-glib.h>

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
  gchar *dmx_device;
  int dmx_speed;
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
  NULL,
  250000,
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
  g_free(app->config_filename);
  if (app->config_filename) g_key_file_unref(app->config_file);
  g_free(app->dmx_device);
}

static const gchar *
device_type_to_string(guint type)
{
  GEnumValue *v =
    g_enum_get_value(g_type_class_peek(C2IP_DEVICE_TYPE_ENUM_TYPE), type);
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
    g_warning("No matching device found when changing value");
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
  C2IPConnectionValues *values;
  C2IPFunction *func;
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
  values = get_values(app, type, name);
  if (!values) return;
  func = c2ip_connection_values_get_function(values, id);
  if (!func) {
    g_warning("No matching function found for mapping");
    return;
  }
  if (strcmp(attr, "channel") == 0) {
    GError *err;
    guint channel = g_value_get_int64(gvalue);
    gfloat min = 0;
    gfloat max = 0;
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
  gchar *range_str;
  GInetAddressMask *range;
  range_str = g_key_file_get_string(app->config_file, "C2IP", "Range", err);
  if (!range_str) {
    g_clear_error(err);
    g_warning("No range given. Connecting to localhost");
    range_str = g_strdup("127.0.0.1/32");
  }
  range = g_inet_address_mask_new_from_string(range_str, err);
  g_free(range_str);
  if (!range) {
    return FALSE;
  }
  app->c2ip_scanner = c2ip_scan_new();
  g_signal_connect(app->c2ip_scanner, "device-found",
		   (GCallback)device_found, app);
  if (!c2ip_scan_start(app->c2ip_scanner, range, err)) {
    g_object_unref(range);
    return FALSE;
  }
  g_object_unref(range);
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

static void
export_mapping(AppContext *app, C2IPFunction *value)
{
  guint vtype = c2ip_function_get_value_type(value);
  gfloat min;
  gfloat max;
  gboolean map = TRUE;
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
  default:
    map = FALSE;
  }
  if (map) {
    guint prefix_len;
    C2IPDevice *dev = c2ip_function_get_device(value);
    GString *str = g_string_new("dmxmap/");
    append_device_path(str, dev);
    g_string_append_printf(str, "/%d/", c2ip_function_get_id(value));
    prefix_len = str->len;
    g_string_append(str, "channel");
    http_server_set_int(app->http_server, str->str, 0, NULL);
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
}

static void
c2ip_function_changed(C2IPConnectionValues *values, C2IPFunction *func,
		      AppContext *app)
{
  export_function(app, func);
}


static void
new_connection(C2IPConnectionManager *cm, C2IPConnection *conn, guint device_type, const gchar *name, AppContext *app)
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
  table = g_key_file_get_string(app->config_file, "DB", "Table", err);
  if (!db_filename) {
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

    if (dmx_c2ip_mapper_get_minmax(mapper, dev_type, dev_name, func_id,
				   &min, &max)) {
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
}

static gboolean
setup_mapper(AppContext *app, GError **err)
{
  app->mapper = dmx_c2ip_mapper_new(app->db, "dmx_c2ip_map", err);
  if (!app->mapper) return FALSE;
  g_signal_connect(app->mapper, "mapping-changed",
		   (GCallback)dmx_mapping_changed, app);
  g_signal_connect(app->mapper, "mapping-removed",
		   (GCallback)dmx_mapping_removed, app);
  return TRUE;
}

const GOptionEntry app_options[] = {
  {"config-file", 'c', 0, G_OPTION_ARG_FILENAME,
   &app_ctxt.config_filename, "Configuration file", "FILE"},
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
    if (!g_key_file_load_from_file(app_ctxt.config_file,
				   app_ctxt.config_filename,
				   G_KEY_FILE_NONE, &err)) {
      g_printerr("Failed to read configuration file: %s\n", err->message);
      app_cleanup(&app_ctxt);
      return EXIT_FAILURE;
    }
  }
  if (!setup_db(&app_ctxt, &err)) {
    g_printerr("Failed to setup db: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  if (!setup_mapper(&app_ctxt, &err)) {
    g_printerr("Failed to setup DMX mapper: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  app_ctxt.dmx_device =
    g_key_file_get_string(app_ctxt.config_file, "DMXPort", "Device", &err);
  if (!app_ctxt.dmx_device) {
    g_printerr("No device: %s, DMX disabled\n", err->message);
    g_clear_error(&err);
  } else {
    app_ctxt.dmx_speed =
      g_key_file_get_integer(app_ctxt.config_file, "DMXPort", "Speed", &err);
    if (err) {
      app_ctxt.dmx_speed = 250000;
      g_clear_error(&err);
    }
    
    app_ctxt.dmx_recv = dmx_recv_new(app_ctxt.dmx_device, &err);
    if (!app_ctxt.dmx_recv) {
      g_printerr("Failed setup DMX port: %s\n", err->message);
      app_cleanup(&app_ctxt);
      return EXIT_FAILURE;
    }
    g_signal_connect(app_ctxt.dmx_recv, "new-packet", (GCallback)dmx_packet, &app_ctxt);
  }
  
  app_ctxt.http_server = http_server_new();
  configure_http_server(&app_ctxt);
  if (!http_server_start(app_ctxt.http_server, &err)) {
    g_printerr("Failed to setup HTTP server: %s\n", err->message);
    g_clear_object(&app_ctxt.http_server);
    g_clear_error(&err);
  }
  
  if (!setup_c2ip(&app_ctxt, &err)) {
    g_printerr("Failed to set up C2IP: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  loop = g_main_loop_new(NULL, FALSE);
  g_unix_signal_add(SIGINT, sigint_handler, loop);
  g_debug("Starting");
  g_main_loop_run(loop);
  g_main_loop_unref(loop);
  g_debug("Exiting");
  app_cleanup(&app_ctxt);
#ifdef MEMPROFILE
  g_mem_profile ();
#endif
  return EXIT_SUCCESS;
}
