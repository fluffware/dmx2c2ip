#ifndef __DMX_C2IP_MAPPER_H__E1ROHMG2OP__
#define __DMX_C2IP_MAPPER_H__E1ROHMG2OP__

#include <sqlite/sqlite3.h>
#include <glib-object.h>
#include <c2ip_connection_values.h>

#define DMX_C2IP_MAPPER_TYPE                  (dmx_c2ip_mapper_get_type ())
#define DMX_C2IP_MAPPER(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), DMX_C2IP_MAPPER_TYPE, DMXC2IPMapper))
#define IS_DMX_C2IP_MAPPER(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), DMX_C2IP_MAPPER_TYPE))
#define DMX_C2IP_MAPPER_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), DMX_C2IP_MAPPER_TYPE, DMXC2IPMapperClass))
#define IS_DMX_C2IP_MAPPER_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), DMX_C2IP_MAPPER_TYPE))
#define DMX_C2IP_MAPPER_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), DMX_C2IP_MAPPER_TYPE, DMXC2IPMapperClass))

typedef struct _DMXC2IPMapper DMXC2IPMapper;
typedef struct _DMXC2IPMapperClass DMXC2IPMapperClass;

enum {
  DMX_C2IP_MAPPER_FLAG_READABLE = 0x01,
  DMX_C2IP_MAPPER_FLAG_WRITABLE = 0x02,
  DMX_C2IP_MAPPER_FLAG_HAS_INFO = 0x04
};

GType
dmx_c2ip_mapper_get_type(void);

DMXC2IPMapper *
dmx_c2ip_mapper_new(sqlite3 *db, const gchar *table, GError **err);

gboolean
dmx_c2ip_mapper_add_map(DMXC2IPMapper *mapper, guint channel,
			C2IPValue *value,
			gfloat min, gfloat max, GError **err);

gboolean
dmx_c2ip_mapper_set_channel(DMXC2IPMapper *mapper,
			    guint channel, guint value, GError **err);


#endif /* __DMX_C2IP_MAPPER_H__E1ROHMG2OP__ */
