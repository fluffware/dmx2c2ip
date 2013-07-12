#ifndef __C2IP_VALUE_H__QVX66S5ML6__
#define __C2IP_VALUE_H__QVX66S5ML6__

#include <glib-object.h>
#include <c2ip_device.h>

#define C2IP_VALUE_TYPE_ENUM_TYPE (c2ip_value_type_enum_get_type())

#define C2IP_VALUE_FLAGS_TYPE (c2ip_value_flags_get_type())

GType
c2ip_value_type_enum_get_type(void);

#define C2IP_VALUE_TYPE                  (c2ip_value_get_type ())
#define C2IP_VALUE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), C2IP_VALUE_TYPE, C2IPValue))
#define IS_C2IP_VALUE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), C2IP_VALUE_TYPE))
#define C2IP_VALUE_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), C2IP_VALUE_TYPE, C2IPValueClass))
#define IS_C2IP_VALUE_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), C2IP_VALUE_TYPE))
#define C2IP_VALUE_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), C2IP_VALUE_TYPE, C2IPValueClass))

typedef struct _C2IPValue C2IPValue;
typedef struct _C2IPValueClass C2IPValueClass;

enum {
  C2IP_VALUE_FLAG_READABLE = 0x01,
  C2IP_VALUE_FLAG_WRITABLE = 0x02,
  C2IP_VALUE_FLAG_HAS_INFO = 0x04
};

GType
c2ip_value_get_type(void);

C2IPValue *
c2ip_value_new(guint id, guint type);

gchar *
c2ip_value_to_string(const C2IPValue *value);

void
c2ip_value_take_option(C2IPValue *value, guint n, gchar *name);

const gchar *
c2ip_value_get_option(const C2IPValue *value, guint n);

typedef gboolean (*C2IPValueOptionFunc)(guint n, const gchar *name,
				       gpointer user_data);

void
c2ip_value_options_foreach(const C2IPValue *value,
			   C2IPValueOptionFunc func, gpointer user_data);

guint
c2ip_value_get_id(const C2IPValue *value);

const gchar *
c2ip_value_get_name(const C2IPValue *value);

guint
c2ip_value_get_value_type(const C2IPValue *value);

const gchar *
c2ip_value_get_value_type_string(const C2IPValue *value);

guint
c2ip_value_get_flags(const C2IPValue *value);

guint
c2ip_value_set_flags(C2IPValue *value, guint flags, guint mask);

const GValue *
c2ip_value_get_value(const C2IPValue *value);

const GValue *
c2ip_value_set_value(C2IPValue *value, const GValue *v);

const gchar *
c2ip_value_get_unit(const C2IPValue *value);

const gchar *
c2ip_value_set_unit(C2IPValue *value, const gchar *unit);

C2IPDevice *
c2ip_value_get_device(C2IPValue *value);

C2IPDevice *
c2ip_value_set_device(C2IPValue *value, C2IPDevice *dev);

#endif /* __C2IP_VALUE_H__QVX66S5ML6__ */
