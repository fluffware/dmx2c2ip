#include <glib-object.h>

#define C2IP_DEVICE_TYPE                  (c2ip_device_get_type ())
#define C2IP_DEVICE(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), C2IP_DEVICE_TYPE, C2IPDevice))
#define IS_C2IP_DEVICE(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), C2IP_DEVICE_TYPE))
#define C2IP_DEVICE_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), C2IP_DEVICE_TYPE, C2IPDeviceClass))
#define IS_C2IP_DEVICE_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), C2IP_DEVICE_TYPE))
#define C2IP_DEVICE_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), C2IP_DEVICE_TYPE, C2IPDeviceClass))

typedef struct _C2IPDevice C2IPDevice;
typedef struct _C2IPDeviceClass C2IPDeviceClass;


GType
c2ip_device_get_type(void);

C2IPDevice *
c2ip_device_new();

guint
c2ip_device_get_device_type(const C2IPDevice *dev);

guint
c2ip_device_set_device_type(C2IPDevice *dev, guint type);

const gchar *
c2ip_device_get_device_name(const C2IPDevice *dev);

const gchar *
c2ip_device_set_device_name(C2IPDevice *dev, const gchar *name);


const gchar *
c2ip_device_get_alias(const C2IPDevice *dev);

const gchar *
c2ip_device_set_alias(C2IPDevice *dev, const gchar *alias);


const gchar *
c2ip_device_get_device_id(const C2IPDevice *dev);

const gchar *
c2ip_device_set_device_id(C2IPDevice *dev, const gchar *id);
