#ifndef __HTTPD_H__QAEOTYJXN0__
#define __HTTPD_H__QAEOTYJXN0__
#include <glib-object.h>

#define HTTP_SERVER_ERROR (http_server_error_quark())
enum {
  HTTP_SERVER_ERROR_START_FAILED = 1
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

#endif /* __HTTPD_H__QAEOTYJXN0__ */