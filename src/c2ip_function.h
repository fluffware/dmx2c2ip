#ifndef __C2IP_FUNCTION_H__QVX66S5ML6__
#define __C2IP_FUNCTION_H__QVX66S5ML6__

#include <glib-object.h>
#include <c2ip_device.h>

#define C2IP_FUNCTION_TYPE_ENUM_TYPE (c2ip_function_type_enum_get_type())

#define C2IP_FUNCTION_FLAGS_TYPE (c2ip_function_flags_get_type())

GType
c2ip_function_type_enum_get_type(void);

#define C2IP_FUNCTION_TYPE                  (c2ip_function_get_type ())
#define C2IP_FUNCTION(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), C2IP_FUNCTION_TYPE, C2IPFunction))
#define IS_C2IP_FUNCTION(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), C2IP_FUNCTION_TYPE))
#define C2IP_FUNCTION_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), C2IP_FUNCTION_TYPE, C2IPFunctionClass))
#define IS_C2IP_FUNCTION_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), C2IP_FUNCTION_TYPE))
#define C2IP_FUNCTION_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), C2IP_FUNCTION_TYPE, C2IPFunctionClass))

typedef struct _C2IPFunction C2IPFunction;
typedef struct _C2IPFunctionClass C2IPFunctionClass;

enum {
  C2IP_FUNCTION_FLAG_READABLE = 0x01,
  C2IP_FUNCTION_FLAG_WRITABLE = 0x02,
  C2IP_FUNCTION_FLAG_HAS_INFO = 0x04
};

GType
c2ip_function_get_type(void);

C2IPFunction *
c2ip_function_new(guint id, guint type);

gchar *
c2ip_function_to_string(const C2IPFunction *value);

void
c2ip_function_take_option(C2IPFunction *value, guint n, gchar *name);

const gchar *
c2ip_function_get_option(const C2IPFunction *value, guint n);

typedef gboolean (*C2IPFunctionOptionFunc)(guint n, const gchar *name,
				       gpointer user_data);

void
c2ip_function_options_foreach(const C2IPFunction *value,
			   C2IPFunctionOptionFunc func, gpointer user_data);

guint
c2ip_function_get_id(const C2IPFunction *value);

const gchar *
c2ip_function_get_name(const C2IPFunction *value);

guint
c2ip_function_get_value_type(const C2IPFunction *value);

const gchar *
c2ip_function_get_value_type_string(const C2IPFunction *value);

guint
c2ip_function_get_flags(const C2IPFunction *value);

guint
c2ip_function_set_flags(C2IPFunction *value, guint flags, guint mask);

const GValue *
c2ip_function_get_value(const C2IPFunction *value);

const GValue *
c2ip_function_set_value(C2IPFunction *value, const GValue *v);

const gchar *
c2ip_function_get_unit(const C2IPFunction *value);

const gchar *
c2ip_function_set_unit(C2IPFunction *value, const gchar *unit);

C2IPDevice *
c2ip_function_get_device(C2IPFunction *value);

C2IPDevice *
c2ip_function_set_device(C2IPFunction *value, C2IPDevice *dev);

#endif /* __C2IP_FUNCTION_H__QVX66S5ML6__ */
