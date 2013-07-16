#include "dmx_c2ip_mapper.h"
#include <string.h>

GQuark
dmx_c2ip_mapper_error_quark()
{
  static GQuark error_quark = 0;
  if (error_quark == 0)
    error_quark = g_quark_from_static_string ("dmx-c2ip-mapper-error-quark");
  return error_quark;
}

enum
{
  PROP_0 = 0,
  N_PROPERTIES
};

typedef struct _MapEntry MapEntry;
struct _MapEntry
{
  guint channel;
  guint dev_type;
  gchar *dev_name;
  gint func_id;
  gfloat min;
  gfloat max;
  C2IPFunction *function;
};

struct _DMXC2IPMapper
{
  GObject parent_instance;
  sqlite3 *db;
  gchar *table;
  /* Maps channels to functions. All entries are owned by this map. */
  GSequence *channel_map;
  /* Maps functions to channels */
  GTree *function_map;
};

struct _DMXC2IPMapperClass
{
  GObjectClass parent_class;
  
  /* class members */

  /* Signals */
};

G_DEFINE_TYPE (DMXC2IPMapper, dmx_c2ip_mapper, G_TYPE_OBJECT)

static void
free_entry(gpointer p)
{
  MapEntry *e = p;
  if (e->function) {
    g_object_remove_weak_pointer(G_OBJECT(e->function),
				 (gpointer*)&e->function);
  }
  g_free(e->dev_name);
  g_free(e);
}

static void
dispose(GObject *gobj)
{
  DMXC2IPMapper *mapper = DMX_C2IP_MAPPER(gobj);

  if (mapper->channel_map) {
    g_tree_destroy(mapper->function_map);
    g_sequence_free(mapper->channel_map);
    mapper->channel_map = NULL;
  }
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

static GQuark channel_quark = 0;

static void
dmx_c2ip_mapper_class_init (DMXC2IPMapperClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  
  /* DMXC2IPMapperClass *dev_class = DMX_C2IP_MAPPER_CLASS(klass); */
  gobject_class->dispose = dispose;
  gobject_class->finalize = finalize;
  channel_quark = g_quark_from_static_string("dmx-c2ip-mapper-channel");
}

static gint
function_cmp(gconstpointer a, gconstpointer b)
{
  gint r;
  const MapEntry *ae = a;
  const MapEntry *be = b;
  r = ae->dev_type - be->dev_type;
  if (r != 0) return r;
  r = ae->func_id - be->func_id;
  if (r != 0) return r;
  return strcmp(ae->dev_name, be->dev_name);
}

static gint
channel_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
  const MapEntry *ae = a;
  const MapEntry *be = b;
  return ae->channel - be->channel;
}

static gint
map_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
  gint r;
  const MapEntry *ae = a;
  const MapEntry *be = b;
  r = ae->channel - be->channel;
  if (r != 0) return r;
  return function_cmp(a,b);
}




static void
dmx_c2ip_mapper_init(DMXC2IPMapper *mapper)
{
  mapper->table = NULL;
  mapper->channel_map = g_sequence_new(free_entry);
  mapper->function_map = g_tree_new(function_cmp);
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
			guint type, const gchar *name, guint id,
			gfloat min, gfloat max, GError **err)
{
  MapEntry *e;
  MapEntry *new_entry = g_new(MapEntry, 1);
  new_entry->dev_type = type;
  new_entry->dev_name = g_strdup(name);
  new_entry->func_id = id;
  new_entry->function = NULL;

  e = g_tree_lookup(mapper->function_map, new_entry);
  if (e) {
    /* Function already mapped to another channel. Just change the
       channel and update limits. */
    free_entry(new_entry);
    g_tree_remove(mapper->function_map, e);
    e->channel = channel;
    e->min = min;
    e->max = max;
    g_tree_insert(mapper->function_map, e, e);
    g_sequence_sort(mapper->channel_map, map_cmp, NULL);
  } else {
    new_entry->channel = channel;
    new_entry->function = NULL;
    new_entry->min = min;
    new_entry->max = max;
    g_sequence_insert_sorted(mapper->channel_map, new_entry, map_cmp, NULL);
    g_tree_insert(mapper->function_map, new_entry, new_entry);
  }
  return TRUE;
}

gboolean
dmx_c2ip_mapper_remove_map_func(DMXC2IPMapper *mapper,
				guint type, const gchar *name, guint id)
{
  MapEntry *e;
  MapEntry key;
  key.dev_type = type;
  key.dev_name = (gchar*)name;
  key.func_id = id;
  e = g_tree_lookup(mapper->function_map, &key);
  if (e) {
    GSequenceIter *iter;
    g_tree_remove(mapper->function_map, e);
    iter = g_sequence_lookup(mapper->channel_map, e, map_cmp, NULL);
    g_sequence_remove(iter);
    return TRUE;
  }
  return FALSE;
}

gboolean
dmx_c2ip_mapper_remove_map_channel(DMXC2IPMapper *mapper, guint channel)
{
  MapEntry key;
  GSequenceIter *iter;
  key.channel = channel;
  iter = g_sequence_lookup(mapper->channel_map, &key, channel_cmp, NULL);
  if (iter) {
    MapEntry *e;
    GSequenceIter *i = iter;
    if (!g_sequence_iter_is_begin(i)) {
      i = g_sequence_iter_prev(i);
      while(!g_sequence_iter_is_begin(i)) {
	GSequenceIter *prev = g_sequence_iter_prev(i);
	e = g_sequence_get(i);
	if (e->channel != channel) break;
	g_sequence_remove(i);
	i = prev;
      }
    }
    i = iter;
    while(!g_sequence_iter_is_end(i)) {
      GSequenceIter *next = g_sequence_iter_next(i);
      e = g_sequence_get(i);
      if (e->channel != channel) break;
      g_sequence_remove(i);
      i = next;
    }
    return TRUE;
  } else {
    return FALSE;
  }
}

static gboolean
set_function(MapEntry *e, guint value, GError **err)
{
  if (e->function) {
    GValue v = G_VALUE_INIT;
    GValue transformed = G_VALUE_INIT;
    g_value_init(&v, G_TYPE_FLOAT);
    g_value_init(&v, G_VALUE_TYPE(c2ip_function_get_value(e->function)));
    if (value > 255) value = 255;
    g_value_set_float(&v, e->min + value * (e->max - e->min) / 255);
    if (!g_value_transform(&v, &transformed)) {
      g_set_error(err, DMX_C2IP_MAPPER_ERROR,
		  DMX_C2IP_MAPPER_ERROR_INCOMPATIBLE_VALUE,
		  "Can't convert DMX value to %s",
		  G_VALUE_TYPE_NAME(&transformed));
	return FALSE;
    }
  }
  return TRUE;
}

gboolean
dmx_c2ip_mapper_set_channel(DMXC2IPMapper *mapper,
			    guint channel, guint value, GError **err)
{
  MapEntry key;
  GSequenceIter *iter;
  key.channel = channel;
  iter = g_sequence_lookup(mapper->channel_map, &key, channel_cmp, NULL);
  if (iter) {
    MapEntry *e;
    GSequenceIter *i = iter;
    if (!g_sequence_iter_is_begin(i)) {
      i = g_sequence_iter_prev(i);
      while(!g_sequence_iter_is_begin(i)) {
	e = g_sequence_get(i);
	if (e->channel != channel) break;
	if (!set_function(e, value, err)) return FALSE;
	i = g_sequence_iter_prev(i);
      }
    }
    i = iter;
    while(!g_sequence_iter_is_end(i)) {
      e = g_sequence_get(i);
      if (e->channel != channel) break;
      if (!set_function(e, value, err)) return FALSE;
      i = g_sequence_iter_next(i);
    }
  }
  return TRUE;
}


