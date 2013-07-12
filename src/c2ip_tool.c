#include <stdio.h>
#include <stdlib.h>
#include <glib-unix.h>
#include <c2ip_scan.h>
#include <c2ip_connection_manager.h>
#include <c2ip_connection.h>
#include <c2ip.h>
#include <c2ip_decode.h>
#include <readline/readline.h>
#include <readline/history.h>

static GMainLoop *main_loop = NULL;

static gboolean
sigint_handler(gpointer user_data)
{
  g_main_loop_quit(main_loop);
  return TRUE;
}

typedef struct AppContext AppContext;
struct AppContext
{
  C2IPScan *c2ip_scanner;
  C2IPConnectionManager *c2ip_connection_manager;
  gchar *range_str;
  gboolean no_ping_reply;
  gboolean no_ping;
  guint next_connection_id;
  guint default_connection;
  GTree *connections;
};
  
AppContext app_ctxt  = {
  NULL,
  NULL,
  NULL,
  FALSE,
  FALSE,
  1,
  0,
  NULL
};

static GQuark connection_id_quark;

static gint
conn_id_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
  return (gint)GPOINTER_TO_SIZE(a) - (gint)GPOINTER_TO_SIZE(b);
}

static void
app_init(AppContext *app)
{
  connection_id_quark = g_quark_from_static_string("connection-id-quark");
  app->range_str = g_strdup("127.0.0.1/32");
  app->connections = g_tree_new_full(conn_id_cmp, NULL, NULL, g_object_unref);
}

static void
app_cleanup(AppContext* app)
{
  g_clear_object(&app->c2ip_scanner);
  g_clear_object(&app->c2ip_connection_manager);
  g_tree_destroy(app->connections);
  g_free(app->range_str);
}

static void
start_text()
{
  putchar('\r');
}

static void
end_text()
{
  fflush(stdout);
  rl_on_new_line();
  rl_redisplay();
}


static void
device_found(C2IPScan *scanner,
	     guint type, const gchar *name,
	     GInetAddress *addr, guint port, AppContext *app)
{
  gchar *addr_str = g_inet_address_to_string(addr);
  start_text();
  printf("Found type %d, name '%s' @ %s:%d\n", type, name, addr_str, port);
  end_text();
  g_free(addr_str);
}

static void
set_default_connection(guint id)
{
  gchar prompt[10];
  app_ctxt.default_connection = id;
  if (id > 0) {
    g_snprintf(prompt, sizeof(prompt), "%d>", id);
    rl_set_prompt(prompt);
  } else {
    rl_set_prompt(">");
  }
}

static void
connected(C2IPConnection *conn, AppContext *app)
{
  guint id =  GPOINTER_TO_SIZE(g_object_get_qdata(G_OBJECT(conn), connection_id_quark));
  start_text();
  printf("%d> Connected\n", id);
  end_text();
}

static void
connection_closed(C2IPConnection *conn, AppContext *app)
{
  guint id =  GPOINTER_TO_SIZE(g_object_get_qdata(G_OBJECT(conn), connection_id_quark));
  if (id == app->default_connection) {
    set_default_connection(0);
  }
  g_tree_remove(app->connections, GSIZE_TO_POINTER(id));
  start_text();
  printf("%d> Connection closed\n",id);
  end_text();
}

static void
received_packet(C2IPConnection *conn, guint len, guint8 *packet, AppContext *app)
{
  guint i;
  guint id =  GPOINTER_TO_SIZE(g_object_get_qdata(G_OBJECT(conn), connection_id_quark));
  start_text();
  printf("%d> ", id);
  printf("%04x:", C2IP_U16(&packet[0]));
  for (i = 4; i < len; i++) {
    printf(" %02x", packet[i]);
  }
  printf("\n");
  c2ip_dump(stdout, packet, len);
  end_text();
}


static void
new_connection(C2IPConnectionManager *cm, C2IPConnection *conn,
	       guint device_type, const gchar *name,
	       AppContext *app)
{
  guint id = app->next_connection_id++;
  g_object_set_qdata(G_OBJECT(conn), connection_id_quark, GSIZE_TO_POINTER(id));
  g_object_ref(conn);
  g_tree_insert(app->connections, GSIZE_TO_POINTER(id), conn);
  if (app->default_connection == 0) {
    set_default_connection(id);
  }
  start_text();
  printf("%d> New connection to device %s\n", id, name);
  end_text();
  g_object_set(conn, "reply-ping", !app->no_ping_reply,
	       "ping-interval", app->no_ping ? 0 : 5000, NULL);
  g_signal_connect(conn, "connected", (GCallback)connected, app);
  g_signal_connect(conn, "connection-closed", (GCallback)connection_closed, app);
  g_signal_connect(conn, "received-packet", (GCallback)received_packet, app);
}

static gboolean
setup_c2ip(AppContext *app, const gchar *range_str, GError **err)
{
  GInetAddressMask *range = g_inet_address_mask_new_from_string(range_str, err);
  if (!range) {
    return FALSE;
  }
  app->c2ip_scanner = c2ip_scan_new();
  g_object_set(app->c2ip_scanner,
	       "first-scan-interval", 100,
	       "scan-interval", 0,
	       NULL);
  g_signal_connect(app->c2ip_scanner, "device-found",
		   (GCallback)device_found, app);
  if (!c2ip_scan_start(app->c2ip_scanner, range, err)) {
    g_object_unref(range);
    return FALSE;
  }
  g_object_unref(range);

  app->c2ip_connection_manager = c2ip_connection_manager_new();
  g_signal_connect_swapped(app->c2ip_scanner, "device-found",
			   (GCallback)c2ip_connection_manager_add_device,
			   app->c2ip_connection_manager);
  g_signal_connect(app->c2ip_connection_manager, "new-connection",
		   (GCallback)new_connection, app);
  return TRUE;
}


static void
add_history_if_different(const char *string)
{
  HIST_ENTRY* hist = history_get(history_base  + history_length - 1);
  /* Check if string is equal to last line in history. */
  if (*string == '\0' || (hist && strcmp(hist->line, string) == 0)) return;
  add_history(string);
}

static void
skip_white(const gchar **p)
{
  const gchar *pos = *p;
  while(g_ascii_isspace(*pos)) pos++;
  *p = pos;
}

static const gchar *
parse_token(const gchar **p)
{
  const gchar *start;
  const gchar *pos = *p;
  skip_white(&pos);
  start = pos;
  while(g_ascii_isalpha(*pos)) pos++;
  if (*pos != '\0' && !g_ascii_isspace(*pos)) return NULL;
  *p = pos;
  return start;
}

static gint
parse_hex(const gchar **p)
{
  guint v=0;
  const gchar *pos = *p;
  skip_white(&pos);
  if (!g_ascii_isxdigit(*pos)) return -1;
  v = g_ascii_xdigit_value(*pos++);
  while(g_ascii_isxdigit(*pos)) {
    v = v * 0x10 + g_ascii_xdigit_value(*pos);
    pos++;
  }
  *p = pos;
  return v;
}

static gint
parse_dec(const gchar **p)
{
  guint v=0;
  const gchar *pos = *p;
  skip_white(&pos);
  if (!g_ascii_isdigit(*pos)) return -1;
  v = g_ascii_digit_value(*pos++);
  while(g_ascii_isdigit(*pos)) {
    v = v * 10 + g_ascii_digit_value(*pos);
    pos++;
  }
  *p = pos;
  return v;
}

static gboolean
match_token(const gchar *pos, const gchar *end, const gchar *match)
{
  while(pos < end && *match != '\0') {
    if (*pos != *match) return FALSE;
    pos++;
    match++;
  }
  return *match == '\0';
}

static C2IPConnection *
get_connection(AppContext *app)
{
  C2IPConnection *conn;
  conn = g_tree_lookup(app->connections,
		       GSIZE_TO_POINTER(app->default_connection));
  if (!conn) {
    printf("Connection not found\n");
  }
  return conn;
}

static void
line_handler(char *line)
{
  const char *token;
  const gchar *pos;
  if (!line) {
    g_main_loop_quit(main_loop);
    return;
  }
  
  add_history_if_different (line);
  pos = line;
  token = parse_token(&pos);
  if (!token) {
    printf("Invalid command\n");
    return;
  }
  if (match_token(token, pos, "scan")) {
    GError *err = NULL;
    if (!c2ip_scan_start(app_ctxt.c2ip_scanner, NULL, &err)) {
      printf("Failed to start scan: %s\n", err->message);
      g_clear_error(&err);
    }
  } else if (match_token(token, pos, "send")) {
    GError *err = NULL;
    C2IPConnection *conn;
    gint type;
    guint8 buffer[260];
    guint bpos = 4;
    type = parse_hex(&pos);
    if (type < 0 || type >= 0x10000) {
      printf("Invalid packet type\n");
      return;
    }
    skip_white(&pos);
    if (*pos != ':') {
      printf("Missing ':' after type\n");
      return;
    }
    pos++;
    while(TRUE) {
      gint v;
      skip_white(&pos);
      if (*pos == '\0') break;
      v = parse_hex(&pos);
      if (v < 0 || v >= 0x100) {
	printf("Invalid data byte\n");
	return;
      }
      if (bpos >= sizeof(buffer)) {
	printf("Packet too long for buffer\n");
	return;
      }
      buffer[bpos++] = v;
    }
    buffer[0] = type >> 8;
    buffer[1] = type;
    buffer[2] = (bpos - 4) >> 8;
    buffer[3] = (bpos - 4);
    
    if (!(conn = get_connection(&app_ctxt))) return;
    if (!c2ip_connection_send_raw_packet(conn, buffer, bpos, &err)) {
      printf("Failed to send packet: %s", err->message);
      g_clear_error(&err);
    }
  } else if (match_token(token, pos, "conn")) {
    C2IPConnection *conn;
    gint v = parse_dec(&pos);
    if (v < 0) {
      printf("Invalid connection number\n");
      return;
    }
    if (!(conn = get_connection(&app_ctxt))) return;
    set_default_connection(v);
  } else if (match_token(token, pos, "close")) {
    C2IPConnection *conn;
    if (!(conn = get_connection(&app_ctxt))) return;
    c2ip_connection_close(conn);
  } else if (match_token(token, pos, "getall")) {
    GError *err;
    C2IPConnection *conn;
    if (!(conn = get_connection(&app_ctxt))) return;
    if (!c2ip_connection_send_value_request_all(conn, &err)) {
      printf("Failed to send packet: %s", err->message);
      g_clear_error(&err);
    }
  } else if (match_token(token, pos, "get")) {
    GError *err;
    C2IPConnection *conn;
    gint id = parse_dec(&pos);
    if (id < 0) {
      printf("Invalid ID\n");
      return;
    }
    if (!(conn = get_connection(&app_ctxt))) return;
    if (!c2ip_connection_send_value_request(conn, id, &err)) {
      printf("Failed to send packet: %s", err->message);
      g_clear_error(&err);
    }
  } else if (match_token(token, pos, "options")) {
    GError *err;
    C2IPConnection *conn;
    gint id = parse_dec(&pos);
    if (id < 0) {
      printf("Invalid ID\n");
      return;
    }
    if (!(conn = get_connection(&app_ctxt))) return;
    if (!c2ip_connection_send_option_request(conn, id, &err)) {
      printf("Failed to send packet: %s", err->message);
      g_clear_error(&err);
    }
  } else if (match_token(token, pos, "info")) {
    GError *err;
    C2IPConnection *conn;
    gint id = parse_dec(&pos);
    if (id < 0) {
      printf("Invalid ID\n");
      return;
    }
    if (!(conn = get_connection(&app_ctxt))) return;
    if (!c2ip_connection_send_info_request(conn, id, &err)) {
      printf("Failed to send packet: %s", err->message);
      g_clear_error(&err);
    }
  } else if (match_token(token, pos, "help")) {
    fputs("Commands:\n"
	  "send <type>: <byte> ...\n"
	  "\tSend a packet\n"
	  "conn <connection>\n"
	  "\tSelect connection for send command\n"
	  "help\n"
	  "\tThis help\n"
	  "quit\n"
	  "\tExit program\n"
	  "\n",
	  stdout);
  } else if (match_token(token, pos, "quit")) {
     g_main_loop_quit(main_loop);
  } else {
    printf("Unknown command\n");
  }
  
}

typedef struct _ReadlineSource ReadlineSource;
struct _ReadlineSource
{
  GSource source;
  GPollFD pollfd;
};

static gboolean
rl_source_prepare(GSource    *source,
		  gint       *timeout_)
{
  *timeout_ = -1;
  return FALSE;
}

static gboolean
rl_source_check(GSource *source)
{
  ReadlineSource *rls = (ReadlineSource*)source;
  if (rls->pollfd.revents & G_IO_IN) return TRUE;
  return FALSE;
}

static gboolean
rl_source_dispatch(GSource *source,
		   GSourceFunc callback,
		   gpointer    user_data)
{
  rl_callback_read_char();
  return TRUE;
}
  
static GSourceFuncs readline_source_funcs =
  {
    rl_source_prepare,
    rl_source_check,
    rl_source_dispatch,
    NULL
  };

static void
setup_read_line(AppContext *app)
{
  ReadlineSource *rls;
  rl_callback_handler_install ("> ", line_handler);
  using_history();
  stifle_history(100);
#if 0
  read_history(history_file);
  history_set_pos(history_length);
#endif
  rls = (ReadlineSource*)
    g_source_new(&readline_source_funcs, sizeof(ReadlineSource));
  rls->pollfd.fd = STDIN_FILENO;
  rls->pollfd.events = G_IO_IN;
  g_source_add_poll(&rls->source, &rls->pollfd);
  g_source_attach(&rls->source, NULL);
}
const GOptionEntry app_options[] = {
  {"addr-range", 'r', 0, G_OPTION_ARG_FILENAME,
   &app_ctxt.range_str, "Address range to scan", "RANGE"},
  {"no-ping-reply", 0, 0, G_OPTION_ARG_NONE, &app_ctxt.no_ping_reply,
   "Don't automatically reply to pings", NULL},
  {"no-ping", 0, 0, G_OPTION_ARG_NONE, &app_ctxt.no_ping,
   "Don't automatically send pings", NULL},
  {NULL}
};

int
main(int argc, char *argv[])
{
  GError *err = NULL;
  GOptionContext *opt_ctxt;
#ifdef MEMPROFILE
  g_mem_set_vtable (glib_mem_profiler_table);
#endif
  app_init(&app_ctxt);
  g_type_init();
  opt_ctxt = g_option_context_new (" - send and receive C2IP messages");
  g_option_context_add_main_entries(opt_ctxt, app_options, NULL);
  if (!g_option_context_parse(opt_ctxt, &argc, &argv, &err)) {
    g_printerr("Failed to parse options: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  g_option_context_free(opt_ctxt);
  if (!setup_c2ip(&app_ctxt,app_ctxt.range_str, &err)) {
    g_printerr("Failed to set up C2IP: %s\n", err->message);
    app_cleanup(&app_ctxt);
    return EXIT_FAILURE;
  }
  setup_read_line(&app_ctxt);
  main_loop = g_main_loop_new(NULL, FALSE);
  g_unix_signal_add(SIGINT, sigint_handler, NULL);
  g_debug("Starting");
  g_main_loop_run(main_loop);
  g_main_loop_unref(main_loop);
  g_debug("Exiting");
  putchar('\n');
  rl_callback_handler_remove();
  app_cleanup(&app_ctxt);
#ifdef MEMPROFILE
  g_mem_profile ();
#endif
  return EXIT_SUCCESS;
}
