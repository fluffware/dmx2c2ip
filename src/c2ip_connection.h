#ifndef __C2IP_CONNECTION_H__D1PF1H80VV__
#define __C2IP_CONNECTION_H__D1PF1H80VV__

#include <glib-object.h>
#include <gio/gio.h>

#define C2IP_CONNECTION_ERROR (c2ip_connection_error_quark())
enum {
  C2IP_CONNECTION_ERROR_INVALID_REPLY = 1
};

#define C2IP_CONNECTION_TYPE                  (c2ip_connection_get_type ())
#define C2IP_CONNECTION(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), C2IP_CONNECTION_TYPE, C2IPConnection))
#define IS_C2IP_CONNECTION(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), C2IP_CONNECTION_TYPE))
#define C2IP_CONNECTION_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), C2IP_CONNECTION_TYPE, C2IPConnectionClass))
#define IS_C2IP_CONNECTION_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), C2IP_CONNECTION_TYPE))
#define C2IP_CONNECTION_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), C2IP_CONNECTION_TYPE, C2IPConnectionClass))

typedef struct _C2IPConnection C2IPConnection;
typedef struct _C2IPConnectionClass C2IPConnectionClass;


C2IPConnection *
c2ip_connection_new(void);


#endif /* __C2IP_CONNECTION_H__D1PF1H80VV__ */
