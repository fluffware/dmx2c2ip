#include <config.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#define MHD_PLATFORM_H
#include "microhttpd.h"
#include <string.h>
#include <ctype.h>
#include <glib.h>
#include "httpd.h"
 #include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <json-glib/json-glib.h>

GQuark
http_server_error_quark()
{
  static GQuark error_quark = 0;
  if (error_quark == 0)
    error_quark = g_quark_from_static_string ("http-server-error-quark");
  return error_quark;
}


enum {
  VALUE_CHANGED,
  LAST_SIGNAL
};

static guint http_server_signals[LAST_SIGNAL] = {0 };

enum
{
  PROP_0 = 0,
  PROP_PORT,
  PROP_USER,
  PROP_PASSWORD,
  PROP_HTTP_ROOT,
  PROP_ROOT_FILE,
  PROP_HTTP_SETS_VALUES,
  PROP_USER_CHANGES_SIGNALED,
  N_PROPERTIES
};

struct _HTTPServer
{
  GObject parent_instance;
  GMainContext *signal_context;
  struct MHD_Daemon *daemon;
  gchar *user;
  gchar *password;
  guint port;
  gchar *http_root;
  gchar *root_file; /* File served when accessing the root */
  gboolean http_sets_values;
  gboolean user_changes_signaled;
  
  GRWLock value_lock; /* Protects any access to value_root and its descendants */
  JsonNode *value_root;
  GData *json_nodes; /* A map of JSON nodes */
  
  GMutex generator_lock;
  JsonGenerator *json_generator;
  GMutex parser_lock;
  JsonParser* json_parser;

  GMutex signal_lock;
  GSource *signal_idle_source;
  GData *pending_changes;
};

struct _HTTPServerClass
{
  GObjectClass parent_class;

  /* class members */

  /* Signals */
  void (*value_changed)(HTTPServer *server, GQuark, const GValue *);
};


/* Forward declarations */

JsonNode *
get_node(HTTPServer *server, const gchar *path, GError **err);

static GQuark
insert_value(HTTPServer *server, const gchar *path, const GValue *value,
	     gboolean extend, GError **err);

G_DEFINE_TYPE (HTTPServer, http_server, G_TYPE_OBJECT)

static void
dispose(GObject *gobj)
{
  HTTPServer *server = HTTP_SERVER(gobj);
  if (server->daemon) {
    MHD_stop_daemon(server->daemon);
    server->daemon = NULL;
  }
  /* No locks needed since the daemon isn't running anymore */
  g_clear_object(&server->json_generator);
  g_clear_object(&server->json_parser);
  if (server->value_root) {
    json_node_free(server->value_root);
    g_datalist_clear(&server->json_nodes);
    server->value_root = NULL;
  }
  g_free(server->user);
  server->user = NULL;
  g_free(server->password);
  server->password = NULL;
  g_free(server->http_root);
  server->http_root = NULL;
  g_free(server->root_file);
  server->root_file = NULL;
  g_datalist_clear(&server->pending_changes);
  if (server->signal_idle_source) {
    g_source_destroy(server->signal_idle_source);
    g_source_unref(server->signal_idle_source);
    server->signal_idle_source= NULL;
  }
  if (server->signal_context) {
    g_main_context_unref(server->signal_context);
    server->signal_context = NULL;
  }
  
  G_OBJECT_CLASS(http_server_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  HTTPServer *server = HTTP_SERVER(gobj);
  g_mutex_clear(&server->generator_lock);
  g_mutex_clear(&server->parser_lock);
  g_rw_lock_clear(&server->value_lock);
  g_mutex_clear(&server->signal_lock);
  G_OBJECT_CLASS(http_server_parent_class)->finalize(gobj);
}

static void
set_property (GObject *object, guint property_id,
	      const GValue *value, GParamSpec *pspec)
{
  HTTPServer *server = HTTP_SERVER(object);
  switch (property_id)
    {
    case PROP_PORT:
      server->port = g_value_get_uint(value);
      break;
    case PROP_USER:
      g_free(server->user);
      server->user = g_value_dup_string(value);
      break;
    case PROP_PASSWORD:
      g_free(server->password);
      server->password = g_value_dup_string(value);
      break;
    case PROP_HTTP_ROOT:
      g_free(server->http_root);
      server->http_root = g_value_dup_string(value);
      break;
    case PROP_ROOT_FILE:
      g_free(server->root_file);
      server->root_file = g_value_dup_string(value);
      break;
    case PROP_HTTP_SETS_VALUES:
      server->http_sets_values = g_value_get_boolean(value);
      break;
    case PROP_USER_CHANGES_SIGNALED:
      server->user_changes_signaled = g_value_get_boolean(value);
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
  HTTPServer *server = HTTP_SERVER(object);
  switch (property_id) {
  case PROP_PORT:
    g_value_set_uint(value, server->port);
    break;
  case PROP_USER:
    g_value_set_string(value,server->user);
    break;
  case PROP_PASSWORD:
    g_value_set_string(value, server->password);
    break;
  case PROP_HTTP_ROOT:
    g_value_set_string(value, server->http_root);
    break;
  case PROP_ROOT_FILE:
    g_value_set_string(value, server->root_file);
    break;
  case PROP_HTTP_SETS_VALUES:
    g_value_set_boolean(value, server->http_sets_values);
    break;
  case PROP_USER_CHANGES_SIGNALED:
    g_value_set_boolean(value, server->user_changes_signaled);
    break;
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
value_changed(HTTPServer *server, GQuark path, const GValue *value)
{
}

static void
http_server_class_init (HTTPServerClass *klass)
{
  GParamSpec *properties[N_PROPERTIES];
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
  HTTPServerClass *server_class = HTTP_SERVER_CLASS(klass);
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
  gobject_class->set_property = set_property;
  gobject_class->get_property = get_property;
  server_class->value_changed = value_changed;
  properties[0] = NULL;
  properties[PROP_USER]
    =  g_param_spec_string("user", "User", "User for athentication",
			   NULL, G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_PASSWORD]
    =  g_param_spec_string("password", "Password", "Password for athentication",
			   NULL, G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_PORT]
    = g_param_spec_uint("http-port", "HTTP port", "HTTP port number",
			1, 65535, 8080,
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_HTTP_ROOT]
    =  g_param_spec_string("http-root", "HTTP root", "Root directory",
			   NULL, G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_ROOT_FILE]
    =  g_param_spec_string("root-file", "Root file",
			   "File served for root access.",
			   NULL, G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_HTTP_SETS_VALUES]
    =  g_param_spec_boolean("http-sets-values", "HTTP sets values",
			    "If true the JSON values are directly changed "
			    "by HTTP requests, otherwise the user is only "
			    "signaled but the values are unchanged.",
			    TRUE, G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_USER_CHANGES_SIGNALED]
    =  g_param_spec_boolean("user-changes-signaled", "Changes signaled",
			    "Changes made by the user is signaled back.",
			    TRUE, G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties(gobject_class, N_PROPERTIES, properties);
  http_server_signals[VALUE_CHANGED] =
    g_signal_new("value-changed",
		 G_OBJECT_CLASS_TYPE (gobject_class),
		 G_SIGNAL_RUN_LAST|G_SIGNAL_DETAILED,
		 G_STRUCT_OFFSET(HTTPServerClass, value_changed),
		 NULL, NULL,
		 g_cclosure_marshal_VOID__UINT_POINTER,
		 G_TYPE_NONE, 2, G_TYPE_UINT, G_TYPE_POINTER);
}

static void
http_server_init(HTTPServer *server)
{
  server->daemon = NULL;
  server->user = NULL;
  server->password = NULL;
  server->port = 8080;
  server->http_root = NULL;
  server->root_file = NULL;
  server->http_sets_values = TRUE;
  server->user_changes_signaled = TRUE;
  g_datalist_init(&server->json_nodes);
  server->value_root = json_node_new(JSON_NODE_OBJECT);
  json_node_take_object(server->value_root, json_object_new());
  g_datalist_id_set_data(&server->json_nodes,
			 g_quark_from_static_string(""), server->value_root);
  server->json_generator = NULL;
  server->json_parser = NULL;
  g_mutex_init(&server->generator_lock);
  g_mutex_init(&server->parser_lock);
  g_rw_lock_init(&server->value_lock);
  server->signal_context = g_main_context_ref_thread_default();
  g_mutex_init(&server->signal_lock);
  server->signal_idle_source = NULL;
  g_datalist_init(&server->pending_changes);
}

static void
signal_each_update(GQuark key_id, gpointer data, gpointer user_data)
{
  HTTPServer *server = user_data;
  g_signal_emit(server, http_server_signals[VALUE_CHANGED],
		key_id, key_id, (GValue*)data);
}

static gboolean
idle_notify_modification(gpointer user_data)
{
  HTTPServer *server = user_data;
  GData *pending;
  g_mutex_lock(&server->signal_lock);
  /* Make a private copy of the list and clear the list in the object so
     the server can use it for new updates. */
  pending = server->pending_changes;
  g_datalist_init(&server->pending_changes);
  g_source_unref(server->signal_idle_source);
  server->signal_idle_source = NULL;
  /* Don't need the lock anymore since we have a private copy of the list. */
  g_mutex_unlock(&server->signal_lock);
  /* Emit the signal for each node that has changed */
  g_datalist_foreach(&pending, signal_each_update, server);
  g_datalist_clear(&pending);
  return FALSE;
}

void
notify_modification(HTTPServer *server)
{
  g_mutex_lock(&server->signal_lock);
  if (!server->signal_idle_source) {
    server->signal_idle_source = g_idle_source_new();
    g_source_set_callback (server->signal_idle_source,
			   idle_notify_modification, server, NULL);
    g_source_attach(server->signal_idle_source, server->signal_context);
  }
  g_mutex_unlock(&server->signal_lock);
}

static int
string_content_handler(void *user_data, uint64_t pos, char *buf, size_t max)
{
  memcpy(buf, ((char*)user_data)+pos, max);
  return max;
}

static void
string_content_handler_free(void *user_data)
{
  g_free(user_data);
}

static int
error_response(struct MHD_Connection * connection,
		      int response, const char *response_msg,
		      const char *detail)
{
  gchar *resp_str;
  struct MHD_Response * resp;
  static const char resp_format[] =
    "<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n"
    "<html> <head><title>%d %s</title></head>\n"
    "<body><h1>%s</h1>\n"
    "<p>%s</p>"
    "</body> </html>\n";
  if (!response_msg) response_msg = "Error";
  if(!detail) detail = "";
  resp_str = g_strdup_printf(resp_format, response,
			     response_msg, response_msg, detail);

  resp = MHD_create_response_from_callback(strlen(resp_str), 1024, string_content_handler, resp_str, string_content_handler_free);
  MHD_add_response_header(resp, "Content-Type",
			   "text/html;charset=UTF-8");
  MHD_queue_response(connection, response, resp);
  MHD_destroy_response(resp);
  return MHD_YES;
}



typedef struct _ConnectionContext ConnectionContext;

struct _ConnectionContext
{
  /* Set to NULL if this is a new request */
  int (*request_handler)(HTTPServer *server, ConnectionContext *cc,
			 struct MHD_Connection * connection,
			 const char *url, const char *method,
			 const char *version, const char *upload_data,
			 size_t *upload_data_size);
  gchar *content_type;
  GString *post_content;
};


static gboolean
check_auth(HTTPServer *server, struct MHD_Connection *connection)
{
  gboolean  res;
  char *user;
  char *pass = NULL;
  if (!server->user || !server->password) return TRUE;
  user = MHD_basic_auth_get_username_password (connection, &pass);
  res = user && pass && strcmp(user, server->user) == 0 && strcmp(pass,server->password) == 0;
  if (user) free(user);
  if (pass) free(pass);
  return res;
}

static gboolean
check_filename(const char *url)
{
  unsigned int period_count = 0;
  char c = *url++;
  if (!(g_ascii_isalnum(c) || c == '_')) return FALSE;
  while(*url != '\0') {
    c = *url++;
    if (c == '.') period_count++;
    else { 
      if (!(g_ascii_isalnum(c) || c == '_')) return FALSE;
   }
  }
  if (period_count != 1) return FALSE;
  return TRUE;
}

static void
add_mime_type(struct MHD_Response *resp, const char *filename)
{
  const char *mime;
  char *ext = index(filename, '.');
  ext++;
  if (strcmp("html",ext) == 0 || strcmp("htm",ext) == 0) {
    mime = "text/html; charset=UTF-8";
  } else  if (strcmp("txt",ext) == 0) {
    mime = "text/plain; charset=UTF-8";
  } else  if (strcmp("svg",ext) == 0) {
    mime = "image/svg+xml";
  } else  if (strcmp("png",ext) == 0) {
    mime = "image/png";
  } else  if (strcmp("jpg",ext) == 0) {
    mime = "image/jpg";
  } else  if (strcmp("js",ext) == 0) {
    mime = "application/javascript";
  } else  if (strcmp("xml",ext) == 0) {
    mime = "text/xml";
  } else  if (strcmp("xhtml",ext) == 0) {
    mime = "application/xhtml+xml";
  } else  if (strcmp("ico",ext) == 0) {
    mime = "image/x-icon";
  } 
  MHD_add_response_header(resp, "Content-Type", mime);
}

static int
file_response(HTTPServer *server,
	      struct MHD_Connection * connection, const char *url)
{
  if (server->http_root) {
    if (url[0] == '\0') {
      if (server->root_file)
	url = server->root_file;
    }
    if (check_filename(url)) {
      int fd;
      gchar * filename = g_build_filename(server->http_root, url, NULL);
      fd = open(filename, O_RDONLY);
      if (fd < 0) {
	g_warning("Failed to open file %s: %s", filename, strerror(errno));
      } else {
	struct stat status;
	if (fstat(fd, &status) >= 0) {
	  struct MHD_Response * resp;
	  resp = MHD_create_response_from_fd_at_offset(status.st_size, fd, 0);
	  add_mime_type(resp,filename); 
	  MHD_queue_response(connection, MHD_HTTP_OK, resp);
	  MHD_destroy_response(resp);
	  g_free(filename);
	  return MHD_YES;
	} else {
	  close(fd);
	}
      }
      g_free(filename);
    }
  }
  error_response(connection, MHD_HTTP_NOT_FOUND, "Not Found", NULL);
  return MHD_YES;
}

/* Node tree manipulation */
JsonNode *
get_node(HTTPServer *server, const gchar *pathstr, GError **err)
{
  JsonNode *node;
  if (*pathstr == '/') pathstr++;
  node = g_datalist_id_get_data(&server->json_nodes,
				g_quark_from_string(pathstr));
  if (!node) {
    g_set_error(err,HTTP_SERVER_ERROR, HTTP_SERVER_ERROR_NODE_NOT_FOUND,
		"Node not found");
  }
  return node;
}

static JsonNode *
create_parent_nodes(HTTPServer *server, const char *path, const char *child_path, JsonNode *new_node, GError **err)
{
  JsonNode *node;
  JsonNode *child;
  const gchar *end;
  if (*child_path == '/') child_path++;
  if (*child_path == '\0') return new_node;
  end = child_path;
  while(*end!='/' && *end!='\0') {
    end++;
  }
  if (end == path) return NULL;
  child = create_parent_nodes(server, path, end, new_node, err);
  if (!child) {
    return NULL;
  }
  {
    gchar *n;
    JsonObject *obj;
    node = json_node_new(JSON_NODE_OBJECT);
    if (!node) {
      json_node_free(child);
      g_set_error(err, HTTP_SERVER_ERROR, HTTP_SERVER_ERROR_INTERNAL,
		  "Failed to create node");
      return NULL;
    }
    obj = json_object_new();
    json_node_take_object(node, obj);
    n = g_strndup(child_path, end - child_path);
    json_object_set_member(obj, n, child);
    g_free(n);
    json_node_set_parent(child, node);
    n = g_strndup(path, child_path - path - 1);
    g_datalist_set_data(&server->json_nodes, n, node);
    g_free(n);
  }
  return node;
}

static JsonNode *
create_value_node(const GValue *new_value)
{
   JsonNode *node = json_node_new(JSON_NODE_VALUE);
   json_node_set_value (node, new_value);
   return node;
}

static void
g_value_free(gpointer d)
{
  GValue *v = d;
  g_value_unset(v);
  g_free(v);
}
  
static void
pending_change(HTTPServer *server, const gchar *pathstr, const GValue *value)
{
  GValue *v = g_new0(GValue,1);
  g_value_init(v, G_VALUE_TYPE(value));
  g_value_copy(value, v);
  g_datalist_set_data_full(&server->pending_changes, pathstr, v, g_value_free);
}

static GQuark
insert_value(HTTPServer *server, const gchar *pathstr, const GValue *new_value,
	     gboolean extend, GError **err)
{
  JsonNode *node;
  g_return_val_if_fail(*pathstr != '/', 0);
  node = g_datalist_get_data(&server->json_nodes, pathstr);
  if (node) {
    GValue v = G_VALUE_INIT;
    if (!JSON_NODE_HOLDS_VALUE(node)) {
      g_set_error(err,
		  HTTP_SERVER_ERROR, HTTP_SERVER_ERROR_NODE_HAS_DIFFERENT_TYPE,
		  "Not a value node");
      return 0;
    }
    g_value_init(&v, json_node_get_value_type(node));
    if (!g_value_transform(new_value, &v)) {
      g_set_error(err,
		  HTTP_SERVER_ERROR, HTTP_SERVER_ERROR_NODE_HAS_DIFFERENT_TYPE,
		  "Incompatible value types");
       return 0;
    }
    json_node_set_value(node, &v);
    if (server->user_changes_signaled) {
      pending_change(server, pathstr, &v);
    }
    g_value_unset(&v);
  } else {
    const gchar *path_suffix;
    if (!extend) {
      g_set_error(err,
		  HTTP_SERVER_ERROR, HTTP_SERVER_ERROR_NODE_NOT_FOUND,
		  "Path outside tree");
      return 0;
    }
    path_suffix = pathstr;
    node = server->value_root;
    while(*path_suffix != '\0') {
      switch(JSON_NODE_TYPE(node)) {
      case JSON_NODE_OBJECT:
	{
	  JsonNode *child;
	  gchar *n;
	  const gchar *end = path_suffix;
	  while(*end!='/' && *end!='\0') end++;
	  if (end == path_suffix) {
	    g_set_error(err,
			HTTP_SERVER_ERROR, HTTP_SERVER_ERROR_INVALID_PATH,
			"Empty path element");
	    return 0;
	  }
	  n = g_strndup(path_suffix, end - path_suffix);
	  child = json_object_get_member(json_node_get_object(node), n);
	  if (!child) {
	    JsonNode *value_node = create_value_node(new_value);
	    child = create_parent_nodes(server, pathstr, end, value_node, err);
	    if (!child) {
	      json_node_free(value_node);
	      g_free(n);
	      return 0;
	    }
	    json_object_set_member(json_node_get_object(node), n, child);
	    g_free(n);
	    json_node_set_parent(child, node);
	    g_datalist_set_data(&server->json_nodes, pathstr, value_node);
	    if (server->user_changes_signaled) {
	      pending_change(server, pathstr, new_value);
	    }
	    return TRUE;
	  }
	  g_free(n);
	  node = child;
	  path_suffix = end;
	  if (*path_suffix == '/') path_suffix++;
	}
	break;
      default:
	return 0;
      }
    }
  }
  return g_quark_from_string(pathstr);
}

struct DeleteNodeContext
{
  const gchar *prefix;
  GData **list;
};

static void
delete_node_node_callback(GQuark key_id, gpointer data, gpointer user_data)
{
  struct DeleteNodeContext *ctxt = user_data;
  if (g_str_has_prefix(g_quark_to_string(key_id), ctxt->prefix)) {
    g_datalist_id_remove_no_notify(ctxt->list, key_id);
  }
}

static gboolean
delete_node(HTTPServer *server, const gchar *pathstr, GError **err)
{
  gchar *last;
  struct DeleteNodeContext ctxt;
  JsonNode *node = get_node(server, pathstr, err);
  JsonNode *parent;
  if (!node) return FALSE;
  parent = json_node_get_parent(node);
  last = rindex(pathstr, '/');
  last++;
  json_object_remove_member(json_node_get_object(parent), last);
  ctxt.list = &server->json_nodes;
  ctxt.prefix = pathstr;
  g_datalist_foreach(&server->json_nodes, delete_node_node_callback, &ctxt);
  return TRUE;
}

gboolean
http_server_remove(HTTPServer *server, const gchar *path, GError **err)
{
  return delete_node(server, path, err);
}

GQuark
http_server_set_value(HTTPServer *server, const gchar *path, const GValue *new_value, GError **err)
{
  gboolean res;
  g_rw_lock_writer_lock(&server->value_lock);
  res = insert_value(server, path, new_value, TRUE, err);
  g_rw_lock_writer_unlock(&server->value_lock);
  notify_modification(server);
  return res;
}

gboolean
http_server_get_value(HTTPServer *server, const gchar *path, GValue *value, GError **err)
{
  gboolean res = FALSE;
  JsonNode *node;
  g_rw_lock_reader_lock(&server->value_lock);
  node = get_node(server, path, err);
  if (node) {
    if (JSON_NODE_HOLDS_VALUE(node)) {
      json_node_get_value(node, value);
      res = TRUE;
    }
  }
  g_rw_lock_reader_unlock(&server->value_lock);
  return res;
}



GQuark
http_server_set_boolean(HTTPServer *server, const gchar *path, gboolean value,
			GError **err)
{
  GQuark res;
  GValue v = G_VALUE_INIT;
  g_value_init(&v, G_TYPE_BOOLEAN);
  g_value_set_boolean(&v, value);
  res = http_server_set_value(server, path, &v, err);
  g_value_unset(&v);
  return res;
}

gboolean
http_server_get_boolean(HTTPServer *server,
			const gchar *path, gboolean *value, GError **err)
{
  gboolean res = FALSE;
  JsonNode *node;
  g_rw_lock_reader_lock(&server->value_lock);
  node = get_node(server, path, err);
  if (node) {
    if (JSON_NODE_HOLDS_VALUE(node)) {
      *value = json_node_get_boolean(node);
      res = TRUE;
    }
  }
  g_rw_lock_reader_unlock(&server->value_lock);
  return res;
}

GQuark
http_server_set_int(HTTPServer *server, const gchar *path, gint64 value,
			GError **err)
{
  GQuark res;
  GValue v = G_VALUE_INIT;
  g_value_init(&v, G_TYPE_INT64);
  g_value_set_int64(&v, value);
  res = http_server_set_value(server, path, &v, err);
  g_value_unset(&v);
  return res;
}

gboolean
http_server_get_int(HTTPServer *server,
			const gchar *path, gint64 *value, GError **err)
{
  gboolean res = FALSE;
  JsonNode *node;
  g_rw_lock_reader_lock(&server->value_lock);
  node = get_node(server, path, err);
  if (node) {
    if (JSON_NODE_HOLDS_VALUE(node)) {
      *value = json_node_get_int(node);
      res = TRUE;
    }
  }
  g_rw_lock_reader_unlock(&server->value_lock);
  return res;
}

GQuark
http_server_set_double(HTTPServer *server, const gchar *path, gdouble value,
			GError **err)
{
  GQuark res;
  GValue v = G_VALUE_INIT;
  g_value_init(&v, G_TYPE_DOUBLE);
  g_value_set_double(&v, value);
  res = http_server_set_value(server, path, &v, err);
  g_value_unset(&v);
  return res;
}

gboolean
http_server_get_double(HTTPServer *server,
		       const gchar *path, double *value, GError **err)
{
  gboolean res = FALSE;
  JsonNode *node;
  g_rw_lock_reader_lock(&server->value_lock);
  node = get_node(server, path, err);
  if (node) {
    if (JSON_NODE_HOLDS_VALUE(node)) {
      *value = json_node_get_double(node);
      res = TRUE;
    }
  }
  g_rw_lock_reader_unlock(&server->value_lock);
  return res;
}

GQuark
http_server_set_string(HTTPServer *server, const gchar *path, const gchar *str,
			GError **err)
{
  GQuark res;
  GValue v = G_VALUE_INIT;
  g_value_init(&v, G_TYPE_STRING);
  g_value_set_string(&v, str);
  res = http_server_set_value(server, path, &v, err);
  g_value_unset(&v);
  return res; 
}

gchar *
http_server_get_string(HTTPServer *server,
		       const gchar *path, GError **err)
{
  gchar *str = NULL;
  JsonNode *node;
  g_rw_lock_reader_lock(&server->value_lock);
  node = get_node(server, path, err);
  if (node) {
    if (JSON_NODE_HOLDS_VALUE(node)) {
      str = g_strdup(json_node_get_string(node));
    }
  }
  g_rw_lock_reader_unlock(&server->value_lock);
  return str;
}

static int
json_response(HTTPServer *server, struct MHD_Connection * connection,
	      JsonNode *node)
{
  gchar *resp_str;
  gsize resp_len;
  struct MHD_Response * resp;
  if (node) {
    g_mutex_lock(&server->generator_lock);
    if (!server->json_generator) {
      server->json_generator = json_generator_new();
    }
    json_generator_set_root(server->json_generator, node);
    resp_str = json_generator_to_data (server->json_generator, &resp_len);
    g_mutex_unlock(&server->generator_lock);
  } else {
    resp_str = g_strdup("null");
  }
  resp = MHD_create_response_from_callback(strlen(resp_str), 1024, string_content_handler, resp_str, string_content_handler_free);
  MHD_add_response_header(resp, "Content-Type",
			  "application/json");
   MHD_add_response_header(resp, "Cache-Control",
			  "no-cache");
  MHD_queue_response(connection, MHD_HTTP_OK, resp);
  MHD_destroy_response(resp);
  
  return MHD_YES; 
}

static int
values_response(HTTPServer *server,
	      struct MHD_Connection * connection, const char *url)
{
  int ret;
  JsonNode *root;
  if (!server->value_root) {
    g_warning("No value root");
    return error_response(connection, MHD_HTTP_NOT_FOUND, "Not Found", NULL);
  }
  g_rw_lock_reader_lock(&server->value_lock);
  root = get_node(server, url, NULL);
  if (!root) {
    g_rw_lock_reader_unlock(&server->value_lock);
    return error_response(connection, MHD_HTTP_NOT_FOUND, "Not Found", NULL);
  }
  ret = json_response(server, connection, root);
  g_rw_lock_reader_unlock(&server->value_lock);
  return ret; 
}

static int
handle_GET_request(HTTPServer *server, ConnectionContext *cc,
			 struct MHD_Connection * connection,
			 const char *url, const char *method,
			 const char *version, const char *upload_data,
			 size_t *upload_data_size)
{
  if (*url == '/') {
    url++;
    if (strncmp("values", url, 6) == 0) {
      url+=6;
      if (*url == '/') url++;
      return values_response(server, connection, url);
    } else {
      return file_response(server, connection, url);
    }
  }
  return error_response(connection, MHD_HTTP_NOT_FOUND, "Not Found", NULL);
}

struct MatchObjectData
{
  gboolean match;
  JsonObject *object;
};

static gboolean
match_node(JsonNode *src, JsonNode *dest);

static void
match_object(JsonObject *src, const gchar *name, JsonNode *src_member, gpointer data)
{
  struct MatchObjectData *mo = data;
  JsonNode *dest_member = json_object_get_member(mo->object, name);
  if (dest_member) {
    if (!match_node(src_member, dest_member)) mo->match = FALSE;
  } else {
    mo->match = FALSE;
  }
}

struct MatchArrayData
{
  gboolean match;
  JsonArray *array;
};

static void
match_array(JsonArray *src,guint index, JsonNode *element_node,
	    gpointer data)
{
  struct MatchArrayData *ma = data;
  JsonNode *dest_element = json_array_get_element(ma->array, index);
  if (dest_element) {
    if (!match_node(element_node, dest_element)) ma->match = FALSE;
  } else {
    ma->match = FALSE;
  }
}

/* Make sure all nodes in src exists in dest */
static gboolean
match_node(JsonNode *src, JsonNode *dest)
{
  if (JSON_NODE_TYPE(src) != JSON_NODE_TYPE(dest)) {
    return FALSE;
  }
  switch(JSON_NODE_TYPE(src)) {
  case JSON_NODE_VALUE:
    if (!g_value_type_transformable (json_node_get_value_type(src),
				     json_node_get_value_type(dest)))
      return FALSE;
    break;
  case JSON_NODE_NULL:
    break;
  case JSON_NODE_OBJECT:
    {
      struct MatchObjectData mo;
      JsonObject *src_obj = json_node_get_object(src);
      mo.match = TRUE;
      mo.object = json_node_get_object(dest);
      json_object_foreach_member(src_obj, match_object, &mo);
      return mo.match;
    }
  case JSON_NODE_ARRAY:
    {
      struct MatchArrayData ma;
      JsonArray *src_array = json_node_get_array(src);
      ma.match = TRUE;
      ma.array = json_node_get_array(dest);
      if (json_array_get_length(src_array) >json_array_get_length(ma.array))
	return FALSE;
      json_array_foreach_element(src_array, match_array, &ma);
      return ma.match;
    }
  }
  return TRUE;
}


static void
copy_node(HTTPServer *server, const gchar *pathstr,
	  JsonNode *src, JsonNode *dest);

struct CopyNodeContext
{
  HTTPServer *server;
  const gchar *pathstr;
  union {
    JsonObject *obj;
    JsonArray *array;
  } dest;
};

static void
copy_object(JsonObject *src, const gchar *name, JsonNode *src_member, gpointer data)
{
  struct CopyNodeContext *ctxt = data;
  JsonNode *dest_member = json_object_get_member(ctxt->dest.obj, name);
  if (dest_member) {
    gchar *child_path = g_strconcat(ctxt->pathstr, "/", name, NULL);
    copy_node(ctxt->server, child_path, src_member, dest_member);
    g_free(child_path);
  }
}


static void
copy_array(JsonArray *src,guint index, JsonNode *element_node,
	    gpointer data)
{
  struct CopyNodeContext *ctxt = data;
  JsonNode *dest_element = json_array_get_element(ctxt->dest.array, index);
  if (dest_element) {
    gchar *child_path = g_strdup_printf("%s/%d",ctxt->pathstr, index);
    copy_node(ctxt->server, child_path, element_node, dest_element);
    g_free(child_path);
  }
}

/* Check if the of the value of the node equals the supplied value */
static gboolean
node_value_equal(JsonNode *node, const GValue *value)
{
  switch(json_node_get_value_type(node)) {
  case G_TYPE_INT64:
    return g_value_get_int64(value) == json_node_get_int(node);
  case G_TYPE_DOUBLE:
    return g_value_get_double(value) == json_node_get_double(node);
  case G_TYPE_BOOLEAN:
    return g_value_get_boolean(value) == json_node_get_boolean(node);
  case G_TYPE_STRING:
    return strcmp(g_value_get_string(value),json_node_get_string(node)) == 0;
  }
  return FALSE;
}

static void
copy_node(HTTPServer *server, const gchar *pathstr,
	  JsonNode *src, JsonNode *dest)
{
  switch(JSON_NODE_TYPE(src)) {
  case JSON_NODE_VALUE:
    {
      GValue src_value = G_VALUE_INIT;
      GValue dest_value = G_VALUE_INIT;
      json_node_get_value (src, &src_value);
      g_value_init(&dest_value, json_node_get_value_type(dest));
      g_value_transform(&src_value, &dest_value);
      if (!node_value_equal(dest, &dest_value)) {
	if (server->http_sets_values) {
	  json_node_set_value (dest, &dest_value);
	}
	if (*pathstr == '/') pathstr++;
	pending_change(server, pathstr, &dest_value);
      }
      g_value_unset(&src_value);
      g_value_unset(&dest_value);
    }
    break;
  case JSON_NODE_NULL:
    break;
  case JSON_NODE_OBJECT:
    {
      struct CopyNodeContext ctxt;
      JsonObject *src_obj = json_node_get_object(src);
      ctxt.server = server;
      ctxt.pathstr = pathstr;
      ctxt.dest.obj = json_node_get_object(dest);
      json_object_foreach_member(src_obj, copy_object, &ctxt);
    }
    break;
  case JSON_NODE_ARRAY:
    {
      struct CopyNodeContext ctxt;
      JsonArray *src_array = json_node_get_array(src);
      ctxt.server = server;
      ctxt.pathstr = pathstr;
      ctxt.dest.array = json_node_get_array(dest);
      json_array_foreach_element(src_array, copy_array, &ctxt);
    }
    break;
  }
}



static int
handle_POST_request(HTTPServer *server, ConnectionContext *cc,
			 struct MHD_Connection * connection,
			 const char *url, const char *method,
			 const char *version, const char *upload_data,
			 size_t *upload_data_size)
{
  if (!cc->content_type || strncmp(cc->content_type, "application/json",16) != 0) {
    gchar detail[100];
    g_snprintf(detail, sizeof(detail), "Only application/json supported for POST (got %s)", cc->content_type ? cc->content_type : "none");
    g_debug(detail);
    return error_response(connection, MHD_HTTP_UNSUPPORTED_MEDIA_TYPE,
			  "Unsupported Media TYpe",
			  detail);
  }
 
  if (strncmp("/values", url, 7) != 0) {
    return error_response(connection, MHD_HTTP_NOT_FOUND, "Not Found", NULL);
  }
  url+=7;
  if (!cc->post_content) {
    cc->post_content = g_string_new("");
    return MHD_YES;
  }
  if (*upload_data_size > 0) {
    g_string_append_len(cc->post_content, upload_data, *upload_data_size);
    *upload_data_size = 0;
    return MHD_YES;
  } else {
    int ret;
    GError *err = NULL;
    JsonNode *node;
    g_rw_lock_writer_lock(&server->value_lock);
    node = get_node(server, url, &err);
    if (!node) {
      g_rw_lock_writer_unlock(&server->value_lock);
      return error_response(connection, MHD_HTTP_NOT_FOUND, "Not Found",
			    "Value not found");
    }
    if (!server->json_parser) {
      server->json_parser = json_parser_new();
    }
    if (!json_parser_load_from_data (server->json_parser, cc->post_content->str,
				     cc->post_content->len, &err)) {
      g_rw_lock_writer_unlock(&server->value_lock);
      g_clear_error(&err);
      return error_response(connection, MHD_HTTP_BAD_REQUEST, "Bad Request",
			    "Invalid JSON");
    }
    if (!match_node(json_parser_get_root(server->json_parser) ,node)) {
      g_rw_lock_writer_unlock(&server->value_lock);
      return error_response(connection, MHD_HTTP_NOT_FOUND, "Not Found",
			    "Value not found");
    }
    copy_node(server, url,  json_parser_get_root(server->json_parser) ,node);
    g_debug("POST data '%s'",cc->post_content->str);
    ret = json_response(server, connection, node);
    g_rw_lock_writer_unlock(&server->value_lock);
    notify_modification(server);
    return ret;
  }
}

void request_completed(void *user_data, struct MHD_Connection *connection,
		       void **con_cls, enum MHD_RequestTerminationCode toe)
{
  ConnectionContext *cc = *con_cls;
  if (cc->post_content) {
    g_string_free(cc->post_content, TRUE);
  }
  g_free(cc->content_type);
  g_free(cc);
  *con_cls = NULL;
  /* g_debug("Request completed"); */
}
		       
static int 
request_handler(void *user_data, struct MHD_Connection * connection,
		const char *url, const char *method,
		const char *version, const char *upload_data,
		size_t *upload_data_size, void **con_cls)
{
  int res = MHD_YES;
  HTTPServer *server = user_data;
  ConnectionContext *conctxt = *con_cls;
  if (!conctxt) {
    /* g_debug("New request"); */
    conctxt = g_new(ConnectionContext,1);
    conctxt->request_handler = NULL;
    conctxt->content_type =
      g_strdup(MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
					   "Content-Type"));
    conctxt->post_content = NULL;
    *con_cls = conctxt;

    if (!check_auth(server, connection)) {
      struct MHD_Response *resp;
      static const char resp_str[] =
	"<!DOCTYPE HTML PUBLIC \"-//IETF//DTD HTML//EN\">\n"
	"<html> <head><title>Not authorized</title></head>\n"
	"<body><h1>Not authorized</h1>\n"
	"<p>You have supplied the wrong username and/or password</p>"
	"</body> </html>\n";
      
      resp = MHD_create_response_from_buffer(strlen(resp_str), (char*) resp_str,
					     MHD_RESPMEM_PERSISTENT);
      MHD_add_response_header(resp, "Content-Type",
			      "text/html; UTF-8");
      res = MHD_queue_basic_auth_fail_response (connection, "DMX camera server",
						resp);
      MHD_destroy_response(resp);
      return res;
    }
  }

  if (!conctxt->request_handler) {
    if (strcmp(method, "GET") == 0) {
      conctxt->request_handler = handle_GET_request;
    } else if (strcmp(method, "POST") == 0) {
      conctxt->request_handler = handle_POST_request;
    } else {
      return error_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, "Method Not Allowed", NULL);
    }
  }
  return conctxt->request_handler(server, conctxt, connection,
				  url, method, version,
				  upload_data, upload_data_size);
  }


HTTPServer *
http_server_new(void)
{
  HTTPServer *server = g_object_new (HTTP_SERVER_TYPE, NULL);
  return server;
}

gboolean
http_server_start(HTTPServer *server, GError **err)
{
   static const struct MHD_OptionItem ops[] = {
    { MHD_OPTION_CONNECTION_LIMIT, 10, NULL },
    { MHD_OPTION_CONNECTION_TIMEOUT, 60, NULL },
    { MHD_OPTION_END, 0, NULL }
  };
  
  
   server->daemon =
     MHD_start_daemon((MHD_USE_DEBUG|MHD_USE_THREAD_PER_CONNECTION
		       |MHD_USE_POLL|MHD_USE_PEDANTIC_CHECKS),
		      server->port, NULL, NULL, request_handler, server,
		      MHD_OPTION_ARRAY, ops,
		      MHD_OPTION_NOTIFY_COMPLETED, request_completed, server,
		      MHD_OPTION_END);
   if (!server->daemon) {
     g_set_error(err, HTTP_SERVER_ERROR, HTTP_SERVER_ERROR_START_FAILED,
		 "Failed to start HTTP daemon");
   }
   return TRUE;
}
