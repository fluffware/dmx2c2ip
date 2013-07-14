#include "dmx_c2ip_mapper.h"
  
enum
{
  PROP_0 = 0,
  N_PROPERTIES
};

typedef struct _MapEntry MapEntry;
struct _MapEntry
{
  MapEntry *next;
  C2IPValue *value;
  gfloat min;
  gfloat max;
};

struct _DMXC2IPMapper
{
  GObject parent_instance;
  sqlite3 *db;
  gchar *table;
  GTree *map;
};

struct _DMXC2IPMapperClass
{
  GObjectClass parent_class;
  
  /* class members */

  /* Signals */
};

G_DEFINE_TYPE (DMXC2IPMapper, dmx_c2ip_mapper, G_TYPE_OBJECT)

static void
free_entries(gpointer p)
{
  MapEntry *e = p;
  while(e) {
    MapEntry *next = e->next;
    if (e->value) {
      g_object_remove_weak_pointer(G_OBJECT(e->value), (gpointer*)&e->value);
    }
    g_free(e);
    e = next;
  }
}

static void
dispose(GObject *gobj)
{
  DMXC2IPMapper *mapper = DMX_C2IP_MAPPER(gobj);
  
  g_tree_destroy(mapper->map);
  g_free(mapper->table);
  mapper->table = NULL;
  G_OBJECT_CLASS(dmx_c2ip_mapper_parent_class)->dispose(gobj);
}

static void
finalize(GObject *gobj)
{
  /* DMXC2IPMapper *dev = DMX_C2IP_MAPPER(gobj); */
  G_OBJECT_CLASS(dmx_c2ip_mapper_parent_class)->finalize(gobj);
}



static void
dmx_c2ip_mapper_class_init (DMXC2IPMapperClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
  /* DMXC2IPMapperClass *dev_class = DMX_C2IP_MAPPER_CLASS(klass); */
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
  
}

static gint
channel_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
  return GPOINTER_TO_SIZE(a) -  GPOINTER_TO_SIZE(b);
}

static void
dmx_c2ip_mapper_init(DMXC2IPMapper *mapper)
{
  mapper->table = NULL;
  mapper->map = g_tree_new_full(channel_cmp, NULL, NULL, free_entries);
}

DMXC2IPMapper *
dmx_c2ip_mapper_new(sqlite3 *db, const gchar *table, GError **err)
{
  DMXC2IPMapper *mapper = g_object_new (DMX_C2IP_MAPPER_TYPE, NULL);
  mapper->db = db;
  mapper->table = g_strdup(table);
  return mapper;
}

gboolean
dmx_c2ip_mapper_add_map(DMXC2IPMapper *mapper, guint channel,
			C2IPValue *value,
			gfloat min, gfloat max, GError **err)
{
  MapEntry *e = g_tree_lookup(mapper->map, GSIZE_TO_POINTER(channel));
  if (!e) {
    e = g_new(MapEntry,1);
    e->value = NULL;
    g_tree_insert(mapper->map, GSIZE_TO_POINTER(channel), e);
  } else {
    MapEntry *free_entry = NULL;
    MapEntry *last = NULL;
    while(e) {
      if (!e->value) {
	free_entry = e;
      } else {
	/* Found a matching entry, overwrite it with new value limits */
	if (e->value == value) break;
      }
      last = e;
      e = e->next;
    }
    if (!e) {
      if (free_entry) e = free_entry;
      else {
	e = g_new(MapEntry,1);
	e->value = NULL;
	last->next = e;
      }
    }
    if (!e->value) {
      g_object_add_weak_pointer(G_OBJECT(value), (gpointer*)&e->value);
    }
    e->min = min;
    e->max = max;
  }
  return TRUE;
}

gboolean
dmx_c2ip_mapper_set_channel(DMXC2IPMapper *mapper,
			    guint channel, guint value, GError **err)
{
  return FALSE;
}


