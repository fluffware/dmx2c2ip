#include <stdio.h>
#include <dmx_recv.h>
#include <stdlib.h>
#include <glib-unix.h>
#include "httpd.h"
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
};
  
AppContext app_ctxt  = {
  NULL,
  NULL,
  NULL,
  250000,
  NULL,
  NULL
};

static void
app_init(AppContext *app)
{
}

static void
app_cleanup(AppContext* app)
{
  g_clear_object(&app->dmx_recv);
  g_clear_object(&app->http_server);

  g_free(app->config_filename);
  if (app->config_filename) g_key_file_unref(app->config_file);
  g_free(app->dmx_device);
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

void value_changed_cb(HTTPServer *server, GQuark path, const GValue *value,
		      AppContext *app)
{
  gchar *vstr = g_strdup_value_contents(value);
  g_debug("%s = %s", g_quark_to_string(path),vstr);
  g_free(vstr);
}

static void
configure_http_server(AppContext *app)
{
  GQuark id;
  GError *err = NULL;
  guint port;
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
  
  id = http_server_set_int(app->http_server, "foo", 78, NULL);
  g_signal_connect(app->http_server, "value-changed", (GCallback)value_changed_cb, app);
  http_server_set_double(app->http_server, "bar/0", 3.1415, NULL);
  http_server_set_double(app->http_server, "bar/1", -1.41, NULL);
  http_server_set_boolean(app->http_server, "up", TRUE, NULL);
  http_server_set_string(app->http_server, "name", "DMX server", NULL);
  http_server_set_string(app->http_server, "l1/l2/l3/str1", "Deep", NULL);
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
  app_ctxt.dmx_device =
    g_key_file_get_string(app_ctxt.config_file, "DMXPort", "Device", &err);
  if (!app_ctxt.dmx_device) {
    g_printerr("No device: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
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
  app_ctxt.http_server = http_server_new();
  configure_http_server(&app_ctxt);
  if (!http_server_start(app_ctxt.http_server, &err)) {
    g_printerr("Failed to setup HTTP server: %s\n", err->message);
    g_clear_object(&app_ctxt.http_server);
    g_clear_error(&err);
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
