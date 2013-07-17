#include "c2ip_device.h"
#include "c2ip.h"

typedef struct _C2IPDeviceTypeEnumClass C2IPDeviceTypeEnumClass;
struct _C2IPDeviceTypeEnumClass
{
  GEnumClass enum_class;
};

static const GEnumValue device_types[] =
  {
    {0, "none", "No device"},
    {C2IP_DEVICE_BASE_STATION, "base", "Base station"},
    {C2IP_DEVICE_CAMERA_HEAD, "camera", "Camera head"},
    {C2IP_DEVICE_OCP, "OCP", "OCP"},
  };

static void
c2ip_device_type_enum_class_init(gpointer g_class,
				gpointer class_data)
{
  C2IPDeviceTypeEnumClass *type_class = g_class;
  type_class->enum_class.minimum = 0;
  type_class->enum_class.maximum = 255;
  type_class->enum_class.n_values =sizeof(device_types)/sizeof(device_types[0]);
  type_class->enum_class.values = (GEnumValue*)device_types;
  
}

static const GTypeInfo value_type_enum_info =
{
  sizeof(C2IPDeviceTypeEnumClass),
  NULL,
  NULL,
  c2ip_device_type_enum_class_init,
  NULL,
  NULL,
  0,
  0,
  NULL,
  NULL
};

GType
c2ip_device_type_enum_get_type(void)
{
  static GType type = 0;
  if (!type) {
    type = g_type_register_static(G_TYPE_ENUM, "C2IPDeviceTypeEnum",
				  &value_type_enum_info, 0);
  }
  return type;
}


enum
{
  PROP_0 = 0,
  PROP_DEVICE_TYPE,
  PROP_DEVICE_NAME,
  PROP_ALIAS,
  PROP_DEVICE_ID,
  N_PROPERTIES
};

struct _C2IPDevice
{
  GObject parent_instance;
  guint device_type;
  gchar *device_name;
  gchar *alias;
  gchar *device_id;
};

struct _C2IPDeviceClass
{
  GObjectClass parent_class;
  
  /* class members */

  /* Signals */
};

G_DEFINE_TYPE (C2IPDevice, c2ip_device, G_TYPE_OBJECT)

static void
dispose(GObject *gobj)
{
  C2IPDevice *dev = C2IP_DEVICE(gobj);
  g_free(dev->device_name);
  dev->device_name = NULL;
  g_free(dev->alias);
  dev->alias = NULL;
  g_free(dev->device_id);
  dev->device_id = NULL;
  G_OBJECT_CLASS(c2ip_device_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  /* C2IPDevice *dev = C2IP_DEVICE(gobj); */
  G_OBJECT_CLASS(c2ip_device_parent_class)->finalize(gobj);
}

static void
set_property (GObject *object, guint property_id,
	      const GValue *gvalue, GParamSpec *pspec)
{
  C2IPDevice *dev = C2IP_DEVICE(object);
  switch (property_id)
    {
    case PROP_DEVICE_TYPE:
      dev->device_type = g_value_get_enum(gvalue);
      break;
    case PROP_DEVICE_NAME:
      g_free(dev->device_name);
      dev->device_name = g_value_dup_string(gvalue);
      break;
    case PROP_DEVICE_ID:
      g_free(dev->device_id);
      dev->device_id = g_value_dup_string(gvalue);
      break;
    case PROP_ALIAS:
      g_free(dev->alias);
      dev->alias = g_value_dup_string(gvalue);
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
  C2IPDevice *dev = C2IP_DEVICE(object); 
  switch (property_id) {
  case PROP_DEVICE_TYPE:
    g_value_set_enum(gvalue,dev->device_type);
    break;
  case PROP_DEVICE_NAME:
    g_value_set_string(gvalue,dev->device_name);
    break;
  case PROP_DEVICE_ID:
    g_value_set_string(gvalue,dev->device_id);
    break;
  case PROP_ALIAS:
    g_value_set_string(gvalue,dev->alias);
    break;
  default:
    /* We don't have any other property... */
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
c2ip_device_class_init (C2IPDeviceClass *klass)
{
  GParamSpec *properties[N_PROPERTIES];
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
  /* C2IPDeviceClass *dev_class = C2IP_DEVICE_CLASS(klass); */
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
  gobject_class->set_property = set_property;
  gobject_class->get_property = get_property;
  
  properties[0] = NULL;
  properties[PROP_DEVICE_TYPE] =
    g_param_spec_enum("device-type", "Device type",
		      "Device type",
		      C2IP_DEVICE_TYPE_ENUM_TYPE,
		      0,
		      G_PARAM_READABLE |G_PARAM_STATIC_STRINGS);
  properties[PROP_DEVICE_NAME] =
    g_param_spec_string("device-name", "Device name",
			 "Device name",
			NULL,
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_DEVICE_ID] =
    g_param_spec_string("device-id", "Device ID",
			 "Device ID",
			 NULL,
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  properties[PROP_ALIAS] =
    g_param_spec_string("alias", "Alias",
			 "Alias",
			 NULL,
			G_PARAM_READWRITE |G_PARAM_STATIC_STRINGS);
  g_object_class_install_properties(gobject_class, N_PROPERTIES, properties);
}

static void
c2ip_device_init(C2IPDevice *value)
{
  value->device_type = 0;
  value->device_name = NULL;
  value->device_id = NULL;
  value->alias = NULL;
}

C2IPDevice *
c2ip_device_new(void)
{
  C2IPDevice *dev = g_object_new (C2IP_DEVICE_TYPE, NULL);
  return dev;
}

guint
c2ip_device_get_device_type(const C2IPDevice *dev)
{
  return dev->device_type;
}

guint
c2ip_device_set_device_type(C2IPDevice *dev, guint type)
{
  return (dev->device_type = type);
}

const gchar *
c2ip_device_get_device_name(const C2IPDevice *dev)
{
  return dev->device_name;
}

const gchar *
c2ip_device_set_device_name(C2IPDevice *dev, const gchar *name)
{
  g_free(dev->device_name);
  dev->device_name = g_strdup(name);
  return dev->device_name;
}

const gchar *
c2ip_device_get_alias(const C2IPDevice *dev)
{
  return dev->alias;
}

const gchar *
c2ip_device_set_alias(C2IPDevice *dev, const gchar *alias)
{
  g_free(dev->alias);
  dev->alias = g_strdup(alias);
  return dev->alias;
}
  
const gchar *
c2ip_device_get_device_id(const C2IPDevice *dev)
{
  return dev->device_id;
}

const gchar *
c2ip_device_set_device_id(C2IPDevice *dev, const gchar *id)
{
  g_free(dev->device_id);
  dev->device_id = g_strdup(id);
  return dev->device_id;
}

