#ifndef __C2IP_CONNECTION_MANAGER_H__8KKRI2F16E__
#define __C2IP_CONNECTION_MANAGER_H__8KKRI2F16E__


#include <glib-object.h>
#include <gio/gio.h>

#define C2IP_CONNECTION_MANAGER_ERROR (c2ip_connection_manager_error_quark())
enum {
  C2IP_CONNECTION_MANAGER_ERROR_INVALID_REPLY = 1
};

#define C2IP_CONNECTION_MANAGER_TYPE                  (c2ip_connection_manager_get_type ())
#define C2IP_CONNECTION_MANAGER(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), C2IP_CONNECTION_MANAGER_TYPE, C2IPConnectionManager))
#define IS_C2IP_CONNECTION_MANAGER(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), C2IP_CONNECTION_MANAGER_TYPE))
#define C2IP_CONNECTION_MANAGER_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), C2IP_CONNECTION_MANAGER_TYPE, C2IPConnectionManagerClass))
#define IS_C2IP_CONNECTION_MANAGER_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), C2IP_CONNECTION_MANAGER_TYPE))
#define C2IP_CONNECTION_MANAGER_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), C2IP_CONNECTION_MANAGER_TYPE, C2IPConnectionManagerClass))

typedef struct _C2IPConnectionManager C2IPConnectionManager;
typedef struct _C2IPConnectionManagerClass C2IPConnectionManagerClass;


C2IPConnectionManager *
c2ip_connection_manager_new(void);

gboolean
c2ip_connection_manager_add_device(C2IPConnectionManager *cm,
				   guint type, const gchar *name,
				   GInetAddress *addr, guint port);

#endif /* __C2IP_CONNECTION_MANAGER_H__8KKRI2F16E__ */
