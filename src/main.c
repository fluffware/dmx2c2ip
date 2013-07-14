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
  GTree *values;
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
  NULL,
  NULL
};

static gint
id_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
  return GPOINTER_TO_SIZE(a) -  GPOINTER_TO_SIZE(b);
}

static void
app_init(AppContext *app)
{
  app->values = g_tree_new_full(id_cmp, NULL, NULL, g_object_unref);
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
  g_tree_destroy(app->values);
  app->values = NULL;
  g_free(app->config_filename);
  if (app->config_filename) g_key_file_unref(app->config_file);
  g_free(app->dmx_device);
}

static gpointer
make_device_key(guint type, const gchar *name)
{
  guint id = atoi(name);
  return GSIZE_TO_POINTER(type<<16 | id);
}

static gpointer
make_device_key_from_device(const C2IPDevice *dev)
{
  const gchar *name = c2ip_device_get_device_name(dev);
  guint type = c2ip_device_get_device_type(dev);
  return make_device_key(type, name);
}

static void
append_device_path(GString *str, const C2IPDevice *dev)
{
  g_string_append(str,  c2ip_device_get_device_name(dev));
  g_string_append_c(str, '/');
  switch(c2ip_device_get_device_type(dev)) {
  case C2IP_DEVICE_BASE_STATION:
    g_string_append(str, "base");
    break;
  case C2IP_DEVICE_CAMERA_HEAD:
    g_string_append(str, "camera");
    break;
  case C2IP_DEVICE_OCP:
    g_string_append(str, "OCP");
    break;
  default:
    g_string_append(str, "unknown");
    break;
  }
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

static void
change_c2ip_value(AppContext *app, const gchar *pathstr, const GValue *gvalue)
{
  GValue transformed = G_VALUE_INIT;
  C2IPConnectionValues *values;
  C2IPValue *v;
  gchar *name = g_alloca(strlen(pathstr));
  gchar *typestr;
  gchar *idstr;
  guint type;
  gpointer key;
  strcpy(name, pathstr);
  typestr = index(name, '/');
  if (typestr == NULL || typestr == name) return;
  *typestr++ = '\0';
  idstr = index(typestr, '/');
  if (idstr == NULL || typestr == idstr) return;
  *idstr++ = '\0';
  if (strcmp(typestr, "base") == 0) {
    type = C2IP_DEVICE_BASE_STATION;
  } else if (strcmp(typestr, "camera") == 0) {
    type = C2IP_DEVICE_CAMERA_HEAD;
  } else if (strcmp(typestr, "OCP") == 0) {
    type = C2IP_DEVICE_OCP;
  } else {
    return;
  }
  key = make_device_key(type, name);
  values = g_tree_lookup(app->values, key);
  if (!values) {
    g_warning("No matching device found when changing value");
    return;
  }
  v = c2ip_connection_values_get_value(values, atoi(idstr));
  if (!v) {
    g_warning("No matching ID");
    return;
  }
  g_value_init(&transformed, G_VALUE_TYPE(c2ip_value_get_value(v)));
  if (!g_value_transform(gvalue, &transformed)) {
    g_value_unset(&transformed);
    return;
  }
  c2ip_value_set_value(v, &transformed);
  g_value_unset(&transformed);
  g_debug("Changing value %d %s %s", type, name, idstr);
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
    change_c2ip_value(app, &pathstr[17], value);
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
  
  http_server_set_int(app->http_server, "foo", 78, NULL);
  g_signal_connect(app->http_server, "value-changed", (GCallback)http_value_changed, app);
  http_server_set_double(app->http_server, "bar/0", 3.1415, NULL);
  http_server_set_double(app->http_server, "bar/1", -1.41, NULL);
  http_server_set_boolean(app->http_server, "up", TRUE, NULL);
  http_server_set_string(app->http_server, "name", "DMX server", NULL);
  http_server_set_string(app->http_server, "l1/l2/l3/str1", "Deep", NULL);
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
  g_debug("Found type %d, name '%s' @ %s:%d", type, name, addr_str, port);
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
  gconstpointer key;
  GString *path = g_string_new("functions/values/");
  const C2IPDevice *dev = c2ip_connection_values_get_device(values);
  append_device_path(path, dev);
  http_server_remove(app->http_server, path->str, NULL);
  g_string_assign(path, "functions/attributes/");
  append_device_path(path, dev);
  http_server_remove(app->http_server, path->str, NULL);
  g_string_free(path,TRUE);
  key = make_device_key_from_device(dev);
  g_tree_remove (app->values, key);
  g_object_unref(values);
  g_debug("Connection closed");
}

static gboolean
print_value(C2IPValue *value, gpointer user_data)
{
  gchar *str;
  C2IPDevice *dev = c2ip_value_get_device(value);
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
				  c2ip_value_get_id(value),
				  "?"));
  str = c2ip_value_to_string(value);
  fputs(str,stdout);
  fputc('\n', stdout);
  fflush(stdout);
  g_free(str);
  return FALSE;
}

struct ExportOptions
{
  GString *path;
  gsize prefix_len;
  HTTPServer *server;
  C2IPValue *value;
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
export_options(C2IPValue *value, GString *path, AppContext *app)
{
  gssize prefix_len = path->len;
  struct ExportOptions export;
  g_string_append(path, "options/");
  export.prefix_len = path->len;
  export.path = path;
  export.server = app->http_server;
  export.value = value;
  c2ip_value_options_foreach(value, export_option, &export);
  g_string_truncate(path, prefix_len);
}

/* Export a value too the world through the web server */
static gboolean
export_value(C2IPValue *value, gpointer user_data)
{
  gsize prefix_len;
  AppContext *app = user_data;
  GString *str = g_string_new("functions/values/");
  C2IPDevice *dev = c2ip_value_get_device(value);
  append_device_path(str, dev);
  g_string_append_printf(str, "/%d", c2ip_value_get_id(value));
  switch( c2ip_value_get_value_type(value)) {
  case C2IP_TYPE_U8:
  case C2IP_TYPE_U12:
  case C2IP_TYPE_U16:
  case C2IP_TYPE_S16:
  case C2IP_TYPE_ENUM:
  case C2IP_TYPE_BOOL:
    http_server_set_int(app->http_server, str->str,
			g_value_get_int(c2ip_value_get_value(value)) , NULL);
    break;
  case C2IP_TYPE_STRING:
    http_server_set_string(app->http_server, str->str,
			   g_value_get_string(c2ip_value_get_value(value)) ,
			   NULL);
    break;
  case C2IP_TYPE_FLOAT16:
    http_server_set_double(app->http_server, str->str,
			   g_value_get_float(c2ip_value_get_value(value)) ,
			   NULL);
    break;
  }
  g_string_assign(str, "functions/attributes/");
  append_device_path(str, dev);
  g_string_append_printf(str, "/%d/", c2ip_value_get_id(value));
  prefix_len = str->len;
  g_string_append(str, "type");
  http_server_set_string(app->http_server, str->str,
			 c2ip_value_get_value_type_string(value), NULL);
  g_string_truncate(str, prefix_len);
  g_string_append(str, "name");
  http_server_set_string(app->http_server, str->str,
			 c2ip_value_get_name(value), NULL);
  g_string_truncate(str, prefix_len);
  
  if (c2ip_value_get_unit(value)) {
    g_string_append(str, "unit");
    http_server_set_string(app->http_server, str->str,
			   c2ip_value_get_unit(value), NULL);
    g_string_truncate(str, prefix_len);
  }
  export_options(value, str, app);
  g_string_free(str, TRUE);

  
  return FALSE;
}

static void
values_ready(C2IPConnectionValues *values, AppContext *app)
{
  c2ip_connection_values_foreach(values, export_value, app);
}

static void
c2ip_value_changed(C2IPConnectionValues *values, C2IPValue *value, AppContext *app)
{
  export_value(value, app);
}


static void
new_connection(C2IPConnectionManager *cm, C2IPConnection *conn, guint device_type, const gchar *name, AppContext *app)
{
  C2IPDevice *dev;
  C2IPConnectionValues *values = c2ip_connection_values_new(conn);
  dev = c2ip_connection_values_get_device(values);
  c2ip_device_set_device_type(dev, device_type);
  c2ip_device_set_device_name(dev, name);
  g_tree_insert(app->values, make_device_key_from_device(dev), values);
  g_object_ref(values);
  g_signal_connect(values, "connection-closed",
		   (GCallback)connection_closed, app);
   g_signal_connect(values, "values-ready",
		   (GCallback)values_ready, app);
   g_signal_connect(values, "value-changed",
		   (GCallback)c2ip_value_changed, app);
  
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
