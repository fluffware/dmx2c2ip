#ifndef __HTTPD_H__QAEOTYJXN0__
#define __HTTPD_H__QAEOTYJXN0__
#include <glib-object.h>

#define HTTP_SERVER_ERROR (http_server_error_quark())
enum {
  HTTP_SERVER_ERROR_START_FAILED = 1,
  HTTP_SERVER_ERROR_INVALID_PATH,
  HTTP_SERVER_ERROR_NODE_NOT_FOUND,
  HTTP_SERVER_ERROR_NODE_HAS_DIFFERENT_TYPE,
  HTTP_SERVER_ERROR_INTERNAL
};

#define HTTP_SERVER_TYPE                  (http_server_get_type ())
#define HTTP_SERVER(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), HTTP_SERVER_TYPE, HTTPServer))
#define IS_HTTP_SERVER(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), HTTP_SERVER_TYPE))
#define HTTP_SERVER_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), HTTP_SERVER_TYPE, HTTPServerClass))
#define IS_HTTP_SERVER_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), HTTP_SERVER_TYPE))
#define HTTP_SERVER_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), HTTP_SERVER_TYPE, HTTPServerClass))

typedef struct _HTTPServer HTTPServer;
typedef struct _HTTPServerClass HTTPServerClass;


HTTPServer *
http_server_new(void);

gboolean
http_server_start(HTTPServer *server, GError **err);

GQuark
http_server_set_value(HTTPServer *server, const gchar *path, const GValue *new_value, GError **err);

gboolean
http_server_get_value(HTTPServer *server, const gchar *path, GValue *value, GError **err);

GQuark
http_server_set_boolean(HTTPServer *server, const gchar *path, gboolean value, GError **err);

gboolean
http_server_get_boolean(HTTPServer *server,
			const gchar *path, gboolean *value, GError **err);

GQuark
http_server_set_int(HTTPServer *server, const gchar *path, gint64 value, GError **err);

gboolean
http_server_get_int(HTTPServer *server,
		    const gchar *path, gint64 *value, GError **err);

GQuark
http_server_set_double(HTTPServer *server, const gchar *path, gdouble value, GError **err);

gboolean
http_server_get_double(HTTPServer *server,
		       const gchar *path, double *value, GError **err);

GQuark
http_server_set_string(HTTPServer *server, const gchar *path, const gchar *str, GError **err);

gchar *
http_server_get_string(HTTPServer *server, const gchar *path, GError **err);

gboolean
http_server_remove(HTTPServer *server, const gchar *path, GError **err);

#endif /* __HTTPD_H__QAEOTYJXN0__ */
