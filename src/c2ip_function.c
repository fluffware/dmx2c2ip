#include "c2ip_function.h"
#include "c2ip_strings.h"
#include "c2ip.h"

typedef struct _C2IPFunctionTypeEnumClass C2IPFunctionTypeEnumClass;
struct _C2IPFunctionTypeEnumClass
{
  GEnumClass enum_class;
};

static const GEnumValue value_types[] =
  {
    {C2IP_TYPE_U8,"U8", "U8"},
    {C2IP_TYPE_ENUM,"ENUM", "ENUM"},
    {C2IP_TYPE_BOOL,"BOOL", "BOOL"},
    {C2IP_TYPE_STRING,"STRING", "STRING"},
    {C2IP_TYPE_S16,"S16", "S16"},
    {C2IP_TYPE_U12,"U12", "U12"},
    {C2IP_TYPE_FLOAT16,"FLOAT16", "FLOAT16"}
  };

static void
c2ip_function_type_enum_class_init(gpointer g_class,
				gpointer class_data)
{
  C2IPFunctionTypeEnumClass *type_class = g_class;
  type_class->enum_class.minimum = 0;
  type_class->enum_class.maximum = 255;
  type_class->enum_class.n_values =sizeof(value_types) / sizeof(value_types[0]);
  type_class->enum_class.values = (GEnumValue*)value_types;
  
}

static const GTypeInfo value_type_enum_info =
{
  sizeof(C2IPFunctionTypeEnumClass),
  NULL,
  NULL,
  c2ip_function_type_enum_class_init,
  NULL,
  NULL,
  0,
  0,
  NULL,
  NULL
};

GType
c2ip_function_type_enum_get_type(void)
{
  static GType type = 0;
  if (!type) {
    type = g_type_register_static(G_TYPE_ENUM, "C2IPFunctionTypeEnum",
				  &value_type_enum_info, 0);
  }
  return type;
}


typedef struct _C2IPFunctionFlagsClass C2IPFunctionFlagsClass;
struct _C2IPFunctionFlagsClass
{
  GFlagsClass flags_class;
};

static const GFlagsValue value_flags[] =
  {
    {C2IP_FUNCTION_FLAG_READABLE,"r", "Readable"},
    {C2IP_FUNCTION_FLAG_WRITABLE,"w", "Writable"},
    {C2IP_FUNCTION_FLAG_HAS_INFO,"has-info", "Has info"}
  };

static void
c2ip_function_flags_class_init(gpointer g_class,
			    gpointer class_data)
{
  C2IPFunctionFlagsClass *type_class = g_class;
  type_class->flags_class.mask =
    (C2IP_FUNCTION_FLAG_READABLE|C2IP_FUNCTION_FLAG_WRITABLE
     |C2IP_FUNCTION_FLAG_HAS_INFO);
  type_class->flags_class.n_values =sizeof(value_flags) / sizeof(value_flags[0]);
  type_class->flags_class.values = (GFlagsValue*)value_flags;
  
}

static const GTypeInfo value_flags_info =
{
  sizeof(C2IPFunctionFlagsClass),
  NULL,
  NULL,
  c2ip_function_flags_class_init,
  NULL,
  NULL,
  0,
  0,
  NULL,
  NULL
};

GType
c2ip_function_flags_get_type(void)
{
  static GType type = 0;
  if (!type) {
    type = g_type_register_static(G_TYPE_FLAGS, "C2IPFunctionFlags",
				  &value_flags_info, 0);
  }
  return type;
}
  
enum
{
  PROP_0 = 0,
  PROP_ID,
  PROP_ID_STR,
  PROP_FLAGS,
  PROP_TYPE,
  PROP_VALUE_INT,
  PROP_VALUE_STRING,
  PROP_VALUE_FLOAT,
  PROP_VALUE,
  PROP_OPTIONS,
  PROP_UNIT,
  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

struct _C2IPFunction
{
  GObject parent_instance;
  C2IPDevice *device;
  guint id;
  guint flags;
  gint type;
  GValue value;
  GTree *options;
  gchar *unit;
};

struct _C2IPFunctionClass
{
  GObjectClass parent_class;
  
  /* class members */

  /* Signals */
};

G_DEFINE_TYPE (C2IPFunction, c2ip_function, G_TYPE_OBJECT)

static void
dispose(GObject *gobj)
{
  C2IPFunction *value = C2IP_FUNCTION(gobj);
  if (value->unit) {
    g_free(value->unit);
    value->unit = NULL;
  }
  g_value_unset(&value->value);
  if (value->options) {
    g_tree_destroy(value->options);
    value->options = NULL;
  }
  g_clear_object(&value->device);
  G_OBJECT_CLASS(c2ip_function_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  /* C2IPFunction *conn = C2IP_FUNCTION(gobj); */
  G_OBJECT_CLASS(c2ip_function_parent_class)->finalize(gobj);
}

static void
set_property (GObject *object, guint property_id,
	      const GValue *gvalue, GParamSpec *pspec)
{
  C2IPFunction *value = C2IP_FUNCTION(object);
  switch (property_id)
    {
    case PROP_ID:
      value->id = g_value_get_uint(gvalue);
      break;
    case PROP_FLAGS:
      value->flags = g_value_get_flags(gvalue);
      break;
    case PROP_TYPE:
      value->type = g_value_get_enum(gvalue);
      break;
    case PROP_VALUE_INT:
    case PROP_VALUE_FLOAT:
    case PROP_VALUE_STRING:
      {
	if (!g_value_transform(gvalue, &value->value)) {
	  gchar *vstr = g_strdup_value_contents (gvalue);
	  g_warning("Failed to set ID %d to %s", value->id, vstr);
	  g_free(vstr);
	} else {
	  g_object_notify_by_pspec(object, properties[PROP_VALUE]);
	}
      }
      break;
    case PROP_VALUE:
      {
	const GValue *v =  g_value_get_pointer(gvalue);
	if (!g_value_transform(v, &value->value)) {
	  gchar *vstr = g_strdup_value_contents (gvalue);
	  g_warning("Failed to set ID %d to %s", value->id, vstr);
	  g_free(vstr);
	}
      }
      break;
    case PROP_OPTIONS:
      break;
    case PROP_UNIT:
      g_free(value->unit);
      value->unit = g_value_dup_string(gvalue);
      break;
    default:
       /* We don't have any other property... */
       G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
get_property (GObject *object, guint property_id,
	      GValue *gvalue, GParamSpec *pspec)
{
  C2IPFunction *value = C2IP_FUNCTION(object); 
  switch (property_id) {
  case PROP_ID:
    g_value_set_uint(gvalue,value->id);
    break;
  case PROP_FLAGS:
    g_value_set_flags(gvalue,value->flags);
    break;
  case PROP_TYPE:
    g_value_set_enum(gvalue,value->type);
    break;
  case PROP_VALUE_INT:
  case PROP_VALUE_FLOAT:
  case PROP_VALUE_STRING:
    {
      if (!g_value_transform(&value->value,gvalue)) {
	gchar *vstr = g_strdup_value_contents (gvalue);
	g_warning("Failed to get ID %d as %s", value->id, G_VALUE_TYPE_NAME(gvalue));
	g_free(vstr);
      }
    }
    break;
  case PROP_OPTIONS:
    
    break;
  case PROP_UNIT:
    g_value_set_string(gvalue,value->unit);
    break;
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
c2ip_function_class_init (C2IPFunctionClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
  /* C2IPFunctionClass *conn_class = C2IP_FUNCTION_CLASS(klass); */
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
  gobject_class->set_property = set_property;
  gobject_class->get_property = get_property;
  
  properties[0] = NULL;
  properties[PROP_ID] =
    g_param_spec_uint("id", "ID",
			"Function ID",
		      0, 65535, 0,
			G_PARAM_READABLE |G_PARAM_STATIC_STRINGS);
  properties[PROP_ID_STR] =
    g_param_spec_string("name", "Name",
			 "Name of function, NULL if unknown",
			 NULL,
			G_PARAM_READABLE |G_PARAM_STATIC_STRINGS);
  properties[PROP_FLAGS] =
    g_param_spec_flags("flags", "Flags",
		       "Value flags",
		       C2IP_FUNCTION_FLAGS_TYPE,
		       0,
		       G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_TYPE] =
    g_param_spec_enum("type", "Type",
		       "Value type",
		       C2IP_FUNCTION_TYPE_ENUM_TYPE,
		       0,
		       G_PARAM_READABLE |G_PARAM_STATIC_STRINGS);
  properties[PROP_OPTIONS] =
     g_param_spec_pointer("options", "Options",
			  "Value options for ENUM and BOOL types",
			  G_PARAM_READABLE |G_PARAM_STATIC_STRINGS);
  properties[PROP_UNIT] =
    g_param_spec_string("unit", "Unit",
			 "Physical unit of value",
			 NULL,
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_VALUE_STRING] =
    g_param_spec_string("value-string", "String value",
			 "Value as string",
			 NULL,
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_VALUE_INT] =
    g_param_spec_int("value-int", "Integer value",
			 "Value as integer",
			-32768,32767, 0,
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_VALUE_FLOAT] =
    g_param_spec_float("value-float", "Float value",
			 "Value as floating point",
			-1e6,1e6, 0.0,
		       G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_VALUE] =
    g_param_spec_pointer("value", "Value",
		       "Value as GValue",
		       G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties(gobject_class, N_PROPERTIES, properties);
}

static gint
id_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
  return GPOINTER_TO_SIZE(a) -  GPOINTER_TO_SIZE(b);
}

static void
c2ip_function_init(C2IPFunction *value)
{
  value->id = 0;
  value->flags = 0;
  value->type = 0;
  value->options = g_tree_new_full(id_cmp, NULL, NULL, g_free);
  value->unit = NULL;
}

C2IPFunction *
c2ip_function_new(guint id, guint type)
{
  C2IPFunction *value = g_object_new (C2IP_FUNCTION_TYPE, NULL);
  value->id = id;
  value->type = type;
  switch(type) {
  case C2IP_TYPE_U8:
  case C2IP_TYPE_S16:
  case C2IP_TYPE_U12:
  case C2IP_TYPE_ENUM:
  case C2IP_TYPE_BOOL:
    g_value_init(&value->value, G_TYPE_INT);
    break;
  case C2IP_TYPE_STRING:
    g_value_init(&value->value, G_TYPE_STRING);
    break;
  case C2IP_TYPE_FLOAT16:
    g_value_init(&value->value, G_TYPE_FLOAT);
    break;
  default:
    g_value_init(&value->value, G_TYPE_INT);
    break;
  }
  return value;
}

/**
 * c2ip_take_option:
 * @value: object
 * @n: numerical value for option
 * @name: dynamically allocated name of the option, will be freed by object
 *
 * Add an option
 **/
 
void
c2ip_function_take_option(C2IPFunction *value, guint n, gchar *name)
{
  g_tree_insert(value->options, GSIZE_TO_POINTER(n), name);
}

const gchar *
c2ip_function_get_option(const C2IPFunction *value, guint n)
{
  return (const gchar*)g_tree_lookup(value->options, GSIZE_TO_POINTER(n));
}

struct OptionCB
{
  C2IPFunctionOptionFunc func;
  gpointer data;
};

static gboolean
foreach_option(gpointer key, gpointer value, gpointer data)
{
  struct OptionCB *cb = data;
  return cb->func(GPOINTER_TO_SIZE(key), (const gchar*)value, cb->data);
}

void
c2ip_function_options_foreach(const C2IPFunction *value,
			   C2IPFunctionOptionFunc func, gpointer user_data)
{
  struct OptionCB cb;
  cb.func = func;
  cb.data = user_data;
  g_tree_foreach(value->options, foreach_option, &cb);
}

gchar *
c2ip_function_to_string(const C2IPFunction *value)
{
  GString *str = g_string_new("");
  g_string_append_printf(str,"%d: ", value->id);
  g_string_append(str, g_enum_get_value(g_type_class_peek(C2IP_FUNCTION_TYPE_ENUM_TYPE), value->type)->value_name);
  g_string_append_c(str, ' ');
  g_string_append_c(str, (value->flags & C2IP_FUNCTION_FLAG_READABLE)?'r':'-');
  g_string_append_c(str, (value->flags & C2IP_FUNCTION_FLAG_WRITABLE)?'w':'-');
  g_string_append_c(str, ' ');
  if (value->type == C2IP_TYPE_BOOL || value->type == C2IP_TYPE_ENUM) {
    const gchar *vstr =
      c2ip_function_get_option(value, g_value_get_int(&value->value));
    if (!vstr) {
      g_string_append_printf(str,"Unknown (%d)",
			     g_value_get_int(&value->value));
    } else {
      g_string_append(str, vstr);
    }
  } else {
    gchar *vstr;
    vstr = g_strdup_value_contents(&value->value);
    g_string_append(str, vstr);
    g_free(vstr);
  }
  if (value->unit) {
     g_string_append_c(str, ' ');
     g_string_append(str, value->unit);
  }
  return g_string_free(str, FALSE);
}

guint
c2ip_function_get_id(const C2IPFunction *value)
{
  return value->id;
}

const gchar *
c2ip_function_get_name(const C2IPFunction *value)
{
  return c2ip_string_map_default(c2ip_funtion_name_map,
				 c2ip_funtion_name_map_length,
				 value->id, "?");
}

guint
c2ip_function_get_value_type(const C2IPFunction *value)
{
  return value->type;
}

const gchar *
c2ip_function_get_value_type_string(const C2IPFunction *value)
{
  const gchar *str = 
    g_enum_get_value(g_type_class_peek(C2IP_FUNCTION_TYPE_ENUM_TYPE),
		     value->type)->value_name;
  if (!str) str = "UNKNOWN";
  return str;
}

guint
c2ip_function_get_flags(const C2IPFunction *value)
{
  return value->flags;
}

guint
c2ip_function_set_flags(C2IPFunction *value, guint flags, guint mask)
{
  value->flags = (value->flags & ~mask) | (flags & mask);
  g_object_notify_by_pspec(G_OBJECT(value), properties[PROP_FLAGS]);
  return value->flags;
}

const GValue *
c2ip_function_get_value(const C2IPFunction *value)
{
  return &value->value;
}

const GValue *
c2ip_function_set_value(C2IPFunction *value, const GValue *v)
{
  g_value_copy(v, &value->value);
  g_object_notify_by_pspec(G_OBJECT(value), properties[PROP_VALUE]);
  return &value->value;
}

const gchar *
c2ip_function_get_unit(const C2IPFunction *value)
{
  return value->unit;
}

const gchar *
c2ip_function_set_unit(C2IPFunction *value, const gchar *unit)
{
  g_free(value->unit);
  value->unit = g_strdup(unit);
  g_object_notify_by_pspec(G_OBJECT(value), properties[PROP_UNIT]);
  return value->unit;
}

C2IPDevice *
c2ip_function_get_device(C2IPFunction *value)
{
  return value->device;
}  

C2IPDevice *
c2ip_function_set_device(C2IPFunction *value, C2IPDevice *dev)
{
  g_clear_object(&value->device);
  value->device = dev;
  g_object_ref(dev);
  return dev;
}
