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

GQuark
http_server_error_quark()
{
  static GQuark error_quark = 0;
  if (error_quark == 0)
    error_quark = g_quark_from_static_string ("http-server-error-quark");
  return error_quark;
}

enum {
  DUMMY_SIG,
  LAST_SIGNAL
};

/* static guint http_server_signals[LAST_SIGNAL] = {0 }; */

enum
{
  PROP_0 = 0,
  PROP_PORT,
  PROP_USER,
  PROP_PASSWORD,
  PROP_HTTP_ROOT,
  N_PROPERTIES
};

struct _HTTPServer
{
  GObject parent_instance;
  struct MHD_Daemon *daemon;
  gchar *user;
  gchar *password;
  guint port;
  gchar *http_root;
};

struct _HTTPServerClass
{
  GObjectClass parent_class;

  /* class members */

  /* Signals */

};
G_DEFINE_TYPE (HTTPServer, http_server, G_TYPE_OBJECT)

static void
dispose(GObject *gobj)
{
  HTTPServer *server = HTTP_SERVER(gobj);
  if (server->daemon) {
    MHD_stop_daemon(server->daemon);
    server->daemon = NULL;
  }
  g_free(server->user);
  g_free(server->password);
  g_free(server->http_root);
  G_OBJECT_CLASS(http_server_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
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
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}

static void
http_server_class_init (HTTPServerClass *klass)
{
  GParamSpec *properties[N_PROPERTIES];
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
  /* HTTPServerClass *server_class = HTTP_SERVER_CLASS(klass); */
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
  gobject_class->set_property = set_property;
  gobject_class->get_property = get_property;

  properties[0] = NULL;
  properties[PROP_USER]
    =  g_param_spec_string("user", "user", "User for athentication",
			   NULL, G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_PASSWORD]
    =  g_param_spec_string("password", "pass", "Password for athentication",
			   NULL, G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_PORT]
    = g_param_spec_uint("http-port", "port", "HTTP port number",
			1, 65535, 8080,
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_HTTP_ROOT]
    =  g_param_spec_string("http-root", "root", "Root directory",
			   NULL, G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties(gobject_class, N_PROPERTIES, properties);
}
static void
http_server_init(HTTPServer *server)
{
  server->daemon = NULL;
  server->user = NULL;
  server->password = NULL;
  server->port = 8080;
  server->http_root = NULL;
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

static void
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
			   "text/html; UTF-8");
  MHD_queue_response(connection, response, resp);
  MHD_destroy_response(resp);
}

typedef struct HeaderData
{
  char *auth;
} HeaderData;

static inline void
init_header_data(HeaderData *hd)
{
  hd->auth = NULL;
}

static void
clear_header_data(HeaderData *hd)
{
  if (hd->auth) {
    g_free(hd->auth);
    hd->auth = NULL;
  }
}

static int
collect_request_headers (void *user_data, enum MHD_ValueKind kind,
			 const char *key, const char *value)
{
  HeaderData *hd = user_data;
  if (strcmp(key, "Authorization")==0) {
    hd->auth = g_strdup(value);
  }
  return MHD_YES;
}


static gboolean
check_auth(HTTPServer *server, char *auth)
{
  gchar *pass;
  gsize cred_len;
  while (isspace(*auth)) auth++;
  if (strncmp("Basic", auth,5) != 0) return FALSE;
  auth+=5;
  while (isspace(*auth)) auth++;
  g_base64_decode_inplace(auth, &cred_len);
  auth[cred_len] = '\0';
  pass = auth;
  while (*pass != ':' && *pass != '\0') pass++;
  *pass++ = '\0';
  return strcmp(auth, server->user) == 0 && strcmp(pass,server->password) == 0;
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
  if (strcmp("html",ext) == 0 || strcmp("htm",ext)) {
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
    mime = "application/javascrip";
  } else  if (strcmp("xml",ext) == 0) {
    mime = "text/xml";
  } else  if (strcmp("xhtml",ext) == 0) {
    mime = "application/xhtml+xml";
  } 
  MHD_add_response_header(resp, "Content-Type", mime);
}

static int
file_response(HTTPServer *server,
	      struct MHD_Connection * connection, const char *url)
{
  if (server->http_root && check_filename(url)) {
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
  error_response(connection, MHD_HTTP_NOT_FOUND, "Not Found", NULL);
  return MHD_YES;
}

static int 
request_handler(void *user_data, struct MHD_Connection * connection,
		const char *url, const char *method,
		const char *version, const char *upload_data,
		size_t *upload_data_size, void **con_cls)
{
  HTTPServer *server = user_data;
  HeaderData header;
  init_header_data(&header);
  if (strcmp(method, "GET") != 0) { 
    error_response(connection, MHD_HTTP_METHOD_NOT_ALLOWED, "Method Not Allowed", NULL);
    return MHD_YES;
  }
  MHD_get_connection_values(connection, MHD_HEADER_KIND,
			    collect_request_headers, &header);
  if (server->user && (!header.auth || !check_auth(server, header.auth))) {
    struct MHD_Response *resp;
    resp = MHD_create_response_from_buffer(0, "", MHD_RESPMEM_PERSISTENT);
    MHD_add_response_header(resp, "WWW-Authenticate",
			    "Basic realm=\"DMX camera server\"");
    MHD_add_response_header(resp, "Content-Type",
			    "text/html; charset=UTF-8");

    MHD_queue_response(connection, MHD_HTTP_UNAUTHORIZED, resp);
    MHD_destroy_response(resp);
    error_response(connection, MHD_HTTP_NOT_FOUND, "Not Found", NULL);
    clear_header_data(&header);
    return MHD_YES;
  }
  clear_header_data(&header);

  if (*url == '/') {
    url++;
    return file_response(server, connection, url);
  }
  error_response(connection, MHD_HTTP_NOT_FOUND, "Not Found", NULL);
  return MHD_YES;
}







void
httpd_start(void)
{
 
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
		      MHD_OPTION_END);
   if (!server->daemon) {
     g_set_error(err, HTTP_SERVER_ERROR, HTTP_SERVER_ERROR_START_FAILED,
		 "Failed to start HTTP daemon");
   }
   return TRUE;
}
