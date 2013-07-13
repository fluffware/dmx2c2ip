#ifndef __C2IP_CONNECTION_VALUES_H__VKTC6X6CAL__
#define __C2IP_CONNECTION_VALUES_H__VKTC6X6CAL__

#include <glib-object.h>
#include <c2ip_connection.h>
#include <c2ip_value.h>

#define C2IP_CONNECTION_VALUES_ERROR (c2ip_connection_values_error_quark())
enum {
  C2IP_CONNECTION_VALUES_ERROR_INVALID_ID = 1,
  C2IP_CONNECTION_VALUES_ERROR_INCOMPATIBLE_VALUE
};

#define C2IP_CONNECTION_VALUES_TYPE                  (c2ip_connection_values_get_type ())
#define C2IP_CONNECTION_VALUES(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), C2IP_CONNECTION_VALUES_TYPE, C2IPConnectionValues))
#define IS_C2IP_CONNECTION_VALUES(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), C2IP_CONNECTION_VALUES_TYPE))
#define C2IP_CONNECTION_VALUES_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), C2IP_CONNECTION_VALUES_TYPE, C2IPConnectionValuesClass))
#define IS_C2IP_CONNECTION_VALUES_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), C2IP_CONNECTION_VALUES_TYPE))
#define C2IP_CONNECTION_VALUES_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), C2IP_CONNECTION_VALUES_TYPE, C2IPConnectionValuesClass))

typedef struct _C2IPConnectionValues C2IPConnectionValues;
typedef struct _C2IPConnectionValuesClass C2IPConnectionValuesClass;


GType
c2ip_connection_values_get_value_type(void);

C2IPConnectionValues *
c2ip_connection_values_new(C2IPConnection *conn);

C2IPValue *
c2ip_connection_values_get_value(const C2IPConnectionValues *values, guint id);

/**
 * C2IPValueCallback:
 * @value:
 * @user_data: as supplied in c2ip_connection_values_for_each
 *
 * Returns: FALSE if the iteration should continue or TRUE to stop
 **/

typedef gboolean (*C2IPValueCallback)(C2IPValue *value, gpointer user_data);

void
c2ip_connection_values_foreach(C2IPConnectionValues *values,
				C2IPValueCallback cb, gpointer user_data);

C2IPDevice *
c2ip_connection_values_get_device(C2IPConnectionValues *values);

gboolean
c2ip_connection_values_change_value(C2IPConnectionValues *values,
				    guint id, const GValue *value,
				    GError **err);

#endif /* __C2IP_CONNECTION_VALUES_H__VKTC6X6CAL__ */
