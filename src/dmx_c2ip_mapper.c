#include "dmx_c2ip_mapper.h"
#include "dmx_c2ip_mapper_marshal.h"
#include <string.h>

GQuark
dmx_c2ip_mapper_error_quark()
{
  static GQuark error_quark = 0;
  if (error_quark == 0)
    error_quark = g_quark_from_static_string ("dmx-c2ip-mapper-error-quark");
  return error_quark;
}

enum {
  MAPPING_CHANGED,
  MAPPING_REMOVED,
  LAST_SIGNAL
};

static guint dmx_c2ip_mapper_signals[LAST_SIGNAL] = {0 };

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

  sqlite3_stmt *stmt_add_mapping;
  sqlite3_stmt *stmt_remove_mapping;
  sqlite3_stmt *stmt_change_mapping;
};

struct _DMXC2IPMapperClass
{
  GObjectClass parent_class;
  
  /* class members */

  /* Signals */
  
  /* Also used for new mappings*/
  void (*mapping_changed)(DMXC2IPMapper *mapper, guint channel, guint dev_type,
			  const gchar *dev_name, guint func_id);
  
  void (*mapping_removed)(DMXC2IPMapper *mapper, guint channel, guint dev_type,
			  const gchar *dev_name, guint func_id);
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
clear_statement(sqlite3_stmt **stmt)
{
  if (*stmt) {
    sqlite3_finalize(*stmt);
    *stmt = NULL;
  }
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
  
  clear_statement(&mapper->stmt_add_mapping);
  clear_statement(&mapper->stmt_remove_mapping);
  clear_statement(&mapper->stmt_change_mapping);
  
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

  dmx_c2ip_mapper_signals[MAPPING_CHANGED] =
    g_signal_new("mapping-changed",
		 G_OBJECT_CLASS_TYPE (gobject_class),
		 G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(DMXC2IPMapperClass, mapping_changed),
		 NULL, NULL,
		 dmx_c2ip_mapper_marshal_VOID__UINT_UINT_STRING_UINT,
		 G_TYPE_NONE, 4,
		 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT);
  dmx_c2ip_mapper_signals[MAPPING_REMOVED] =
    g_signal_new("mapping-removed",
		 G_OBJECT_CLASS_TYPE (gobject_class),
		 G_SIGNAL_RUN_LAST,
		 G_STRUCT_OFFSET(DMXC2IPMapperClass, mapping_removed),
		 NULL, NULL,
		 dmx_c2ip_mapper_marshal_VOID__UINT_UINT_STRING_UINT,
		 G_TYPE_NONE, 4,
		 G_TYPE_UINT, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_UINT);
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

  mapper->stmt_add_mapping = NULL;
  mapper->stmt_change_mapping = NULL;
  mapper->stmt_remove_mapping = NULL;
}

DMXC2IPMapper *
dmx_c2ip_mapper_new()
{
  DMXC2IPMapper *mapper = g_object_new (DMX_C2IP_MAPPER_TYPE, NULL);
  return mapper;
}


static gboolean
prepare_statement(DMXC2IPMapper *mapper, sqlite3_stmt **stmt,
		  const gchar *stmt_str, GError **err)
{
  gint res;
  GString *str = g_string_new("");
  if (*stmt) {
    sqlite3_finalize(*stmt);
  }

  g_string_printf(str, stmt_str, mapper->table);
  res = sqlite3_prepare_v2(mapper->db, str->str, str->len, stmt, NULL);
  if (res != SQLITE_OK) {
    g_set_error(err, DMX_C2IP_MAPPER_ERROR,
		DMX_C2IP_MAPPER_ERROR_DATABASE_ERROR,
		"Failed to prepare statement '%s': %s",
		str->str, sqlite3_errmsg(mapper->db));
    g_string_free(str, TRUE);
    return FALSE;
  }
  g_string_free(str, TRUE);
  return TRUE;
}
#define DB_COL_DEVICE_TYPE 0
#define DB_COL_DEVICE_NAME 1
#define DB_COL_FUNCTION_ID 2
#define DB_COL_CHANNEL 3
#define DB_COL_MIN 4
#define DB_COL_MAX 5

gboolean
dmx_c2ip_mapper_read_db(DMXC2IPMapper *mapper,
			sqlite3 *db, const gchar *table, GError **err)
{
  char *errmsg;
  sqlite3_stmt *stmt;
  int res;
  GString *str = g_string_new("");

  clear_statement(&mapper->stmt_add_mapping);
  clear_statement(&mapper->stmt_remove_mapping);
  clear_statement(&mapper->stmt_change_mapping);

  mapper->db = db;
  mapper->table = g_strdup(table);
  g_string_printf(str, "create table if not exists %s "
		  "(device_type int, device_name text, "
		  "function_id int, channel int, min real, max real);",
		  mapper->table);
  res = sqlite3_exec(db, str->str, NULL, NULL, &errmsg);
  if (res != SQLITE_OK) {
    g_set_error(err, DMX_C2IP_MAPPER_ERROR,
		DMX_C2IP_MAPPER_ERROR_DATABASE_ERROR,
		"Failed to create DMX map database: %s", errmsg);
    sqlite3_free(errmsg);
    g_string_free(str, TRUE);
    return FALSE;
  }

  g_string_printf(str, "select * from %s;", mapper->table);
  res = sqlite3_prepare_v2(db, str->str, str->len,
			   &stmt, NULL);
  g_string_free(str, TRUE);
  if (res != SQLITE_OK) {
    g_set_error(err, DMX_C2IP_MAPPER_ERROR,
		DMX_C2IP_MAPPER_ERROR_DATABASE_ERROR,
		"Failed to select mappings: %s", sqlite3_errmsg(db));
    return FALSE;
  }
  while((res = sqlite3_step(stmt)) == SQLITE_ROW) {
    gint func_id = sqlite3_column_int(stmt, DB_COL_FUNCTION_ID);
    gint dev_type = sqlite3_column_int(stmt, DB_COL_DEVICE_TYPE);
    const gchar *dev_name = (const gchar*)
      sqlite3_column_text(stmt, DB_COL_DEVICE_NAME);
    gint channel = sqlite3_column_int(stmt, DB_COL_CHANNEL);
    gfloat min = sqlite3_column_double(stmt, DB_COL_MIN);
    gfloat max = sqlite3_column_double(stmt, DB_COL_MAX);
    if (!dmx_c2ip_mapper_add_map(mapper, channel, dev_type, dev_name, func_id,
				 min, max, err))
      {
	sqlite3_finalize(stmt);
	g_string_free(str, TRUE);
	return FALSE;
      }
  }
  sqlite3_finalize(stmt);
  if (res != SQLITE_DONE) {
    g_set_error(err, DMX_C2IP_MAPPER_ERROR,
		DMX_C2IP_MAPPER_ERROR_DATABASE_ERROR,
		 "Failed to get mappings: %s", sqlite3_errmsg(db));
    g_string_free(str, TRUE);
    return FALSE;
  }
  
  if (!prepare_statement(mapper, &mapper->stmt_add_mapping,
			 "insert into %s values (?1, ?2, ?3, ?4, ?5, ?6);",
			 err)) {
    return FALSE;
  }
  if (!prepare_statement(mapper, &mapper->stmt_remove_mapping,
			 "delete from %s where device_type = ?1 "
			 "and device_name = ?2 and function_id = ?3;",
			 err)) {
    return FALSE;
  }

  if (!prepare_statement(mapper, &mapper->stmt_change_mapping,
			 "update %s set min = ?4, max = ?5 "
			 "where device_type = ?1 and device_name = ?2 "
			 "and function_id = ?3;", err)) {
    return FALSE;
  }
							 
  return TRUE;
}

static void
entry_added(DMXC2IPMapper *mapper, MapEntry *e)
{
  if (mapper->stmt_add_mapping) {
    gint res;
    sqlite3_reset(mapper->stmt_add_mapping);
    sqlite3_bind_int(mapper->stmt_add_mapping, 1, e->dev_type);
    sqlite3_bind_text(mapper->stmt_add_mapping, 2, e->dev_name, -1,
		      SQLITE_TRANSIENT);
    sqlite3_bind_int(mapper->stmt_add_mapping, 3, e->func_id);
    sqlite3_bind_int(mapper->stmt_add_mapping, 4, e->channel);
    sqlite3_bind_double(mapper->stmt_add_mapping, 5, e->min);
    sqlite3_bind_double(mapper->stmt_add_mapping, 6, e->max);

    res = sqlite3_step(mapper->stmt_add_mapping);
    if (res != SQLITE_DONE) {
      g_warning("Failed to insert mapping in database: %s",
		sqlite3_errmsg(mapper->db));
    }
  }
  g_signal_emit(mapper, dmx_c2ip_mapper_signals[MAPPING_CHANGED], 0,
		e->channel, e->dev_type, e->dev_name, e->func_id);
}

static void
entry_removed(DMXC2IPMapper *mapper, MapEntry *e)
{
  if (mapper->stmt_remove_mapping) {
    gint res;
    sqlite3_reset(mapper->stmt_remove_mapping);
    sqlite3_bind_int(mapper->stmt_remove_mapping, 1, e->dev_type);
    sqlite3_bind_text(mapper->stmt_remove_mapping, 2, e->dev_name, -1,
		      SQLITE_TRANSIENT);
    sqlite3_bind_int(mapper->stmt_remove_mapping, 3, e->func_id);
   
    res = sqlite3_step(mapper->stmt_remove_mapping);
    if (res != SQLITE_DONE) {
      g_warning("Failed to remove mapping from database: %s",
		sqlite3_errmsg(mapper->db));
    }
  }
  g_signal_emit(mapper, dmx_c2ip_mapper_signals[MAPPING_REMOVED], 0,
		e->channel, e->dev_type, e->dev_name, e->func_id);
}

MapEntry *
add_entry(DMXC2IPMapper *mapper, guint channel,
	  guint type, const gchar *name, guint id,
	  gfloat min, gfloat max)
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
    entry_removed(mapper, e);
    g_tree_remove(mapper->function_map, e);
    e->channel = channel;
    e->min = min;
    e->max = max;
    g_tree_insert(mapper->function_map, e, e);
    g_sequence_sort(mapper->channel_map, map_cmp, NULL);
    entry_added(mapper, e);
    return e;
  } else {
    new_entry->channel = channel;
    new_entry->function = NULL;
    new_entry->min = min;
    new_entry->max = max;
    g_sequence_insert_sorted(mapper->channel_map, new_entry, map_cmp, NULL);
    g_tree_insert(mapper->function_map, new_entry, new_entry);
    entry_added(mapper, new_entry);
    return new_entry;
  } 
}

gboolean
dmx_c2ip_mapper_add_map(DMXC2IPMapper *mapper, guint channel,
			guint type, const gchar *name, guint id,
			gfloat min, gfloat max, GError **err)
{
  add_entry(mapper, channel, type, name, id, min, max);
  return TRUE;
}

gboolean
dmx_c2ip_mapper_add_map_function(DMXC2IPMapper *mapper, guint channel,
				 C2IPFunction *func,
				 gfloat min, gfloat max, GError **err)
{
  C2IPDevice *dev = c2ip_function_get_device(func);
  MapEntry *e = add_entry(mapper, channel,
			  c2ip_device_get_device_type(dev),
			  c2ip_device_get_device_name(dev),
			  c2ip_function_get_id(func),
			  min, max);
  if (e->function) {
    g_object_remove_weak_pointer(G_OBJECT(e->function),
				 (gpointer*)&e->function);
    e->function = NULL;
  }
  e->function = func;
  g_object_add_weak_pointer(G_OBJECT(e->function), (gpointer*)&e->function);
  return TRUE;
}

/**
 * dmx_c2ip_mapper_bind_function:
 * @mapper
 * @func: function to bind
 * @err
 *
 * If there is a mapping to @func, but it's not already bound to a
 * live function. Then this function will bind it. Use this when a new
 * connection is made and functions become available.
 *
 * Returns: TRUE if no error.
 **/

gboolean
dmx_c2ip_mapper_bind_function(DMXC2IPMapper *mapper,
			      C2IPFunction *func,
			      GError **err)
{
  MapEntry *e;
  MapEntry key;
  C2IPDevice *dev = c2ip_function_get_device(func);
  key.dev_type = c2ip_device_get_device_type(dev);
  key.dev_name = (gchar*)c2ip_device_get_device_name(dev);
  key.func_id = c2ip_function_get_id(func);
  e = g_tree_lookup(mapper->function_map, &key);
  if (e) {
    if (!e->function) {
      e->function = func;
      g_object_add_weak_pointer(G_OBJECT(e->function), (gpointer*)&e->function);
    }
  }
  return TRUE;
}

static void
remove_entry(DMXC2IPMapper *mapper, GSequenceIter *iter, MapEntry *e)
{
   entry_removed(mapper, e);
   g_tree_remove(mapper->function_map, e);
   g_sequence_remove(iter);
}

gboolean
dmx_c2ip_mapper_remove_func(DMXC2IPMapper *mapper,
			    guint type, const gchar *name, guint id)
{
  MapEntry *e;
  MapEntry key;
  key.dev_type = type;
  key.dev_name = (gchar*)name;
  key.func_id = id;
  e = g_tree_lookup(mapper->function_map, &key);
  if (e) {
    GSequenceIter *iter =
      g_sequence_lookup(mapper->channel_map, e, map_cmp, NULL);;
    remove_entry(mapper, iter, e);
    return TRUE;
  }
  return FALSE;
}

gboolean
dmx_c2ip_mapper_remove_channel(DMXC2IPMapper *mapper, guint channel)
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
	remove_entry(mapper, i, e);
	i = prev;
      }
    }
    i = iter;
    while(!g_sequence_iter_is_end(i)) {
      GSequenceIter *next = g_sequence_iter_next(i);
      e = g_sequence_get(i);
      if (e->channel != channel) break;
      remove_entry(mapper, i, e);
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
    if (value > 255) value = 255;
    g_value_set_float(&v, e->min + value * (e->max - e->min) / 255);
    g_value_init(&transformed,
		 G_VALUE_TYPE(c2ip_function_get_value(e->function)));
    if (!g_value_transform(&v, &transformed)) {
      g_set_error(err, DMX_C2IP_MAPPER_ERROR,
		  DMX_C2IP_MAPPER_ERROR_INCOMPATIBLE_VALUE,
		  "Can't convert DMX value to %s",
		  G_VALUE_TYPE_NAME(&transformed));
      g_value_unset(&v);
      g_value_unset(&transformed);
      return FALSE;
    }
    g_value_unset(&v);

    c2ip_function_set_value(e->function, &transformed);
    g_value_unset(&transformed);
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
    while(!g_sequence_iter_is_begin(i)) {
      i = g_sequence_iter_prev(i);
      e = g_sequence_get(i);
      if (e->channel != channel) break;
      if (!set_function(e, value, err)) return FALSE;
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


gboolean
dmx_c2ip_mapper_get_function_mapping(DMXC2IPMapper *mapper,
				     guint dev_type, const gchar *dev_name,
				     guint func_id,
				     guint *channel,
				     gfloat *min, gfloat *max)
{
  MapEntry *e;
  MapEntry key;
  key.dev_type = dev_type;
  key.dev_name = (gchar*)dev_name;
  key.func_id = func_id;
  e = g_tree_lookup(mapper->function_map, &key);
  if (e) {
    *channel = e->channel,
    *min = e->min;
    *max = e->max;
    return TRUE;
  }
  return FALSE;
}

static void
entry_changed(DMXC2IPMapper *mapper, MapEntry *e)
{
  if (mapper->stmt_change_mapping) {
    gint res;
    sqlite3_reset(mapper->stmt_change_mapping);
    sqlite3_bind_int(mapper->stmt_change_mapping, 1, e->dev_type);
    sqlite3_bind_text(mapper->stmt_change_mapping, 2, e->dev_name, -1,
		      SQLITE_TRANSIENT);
    sqlite3_bind_int(mapper->stmt_change_mapping, 3, e->func_id);
    sqlite3_bind_double(mapper->stmt_change_mapping, 4, e->min);
    sqlite3_bind_double(mapper->stmt_change_mapping, 5, e->max);

    res = sqlite3_step(mapper->stmt_change_mapping);
    if (res != SQLITE_DONE) {
      g_warning("Failed to change mapping in database: %s",
		sqlite3_errmsg(mapper->db));
    }
  }
    g_signal_emit(mapper, dmx_c2ip_mapper_signals[MAPPING_CHANGED], 0,
		  e->channel, e->dev_type, e->dev_name, e->func_id);  
}

gboolean
dmx_c2ip_mapper_set_min(DMXC2IPMapper *mapper,
			guint dev_type, const gchar *dev_name, guint func_id,
			gfloat min)
{
  MapEntry *e;
  MapEntry key;
  key.dev_type = dev_type;
  key.dev_name = (gchar*)dev_name;
  key.func_id = func_id;
  e = g_tree_lookup(mapper->function_map, &key);
  if (e) {
    e->min = min;
    entry_changed(mapper, e);
    g_signal_emit(mapper, dmx_c2ip_mapper_signals[MAPPING_CHANGED], 0,
		  e->channel, e->dev_type, e->dev_name, e->func_id);
    return TRUE;
  }
  return FALSE;
}

gboolean
dmx_c2ip_mapper_set_max(DMXC2IPMapper *mapper,
			guint dev_type, const gchar *dev_name, guint func_id,
			gfloat max)
{
  MapEntry *e;
  MapEntry key;
  key.dev_type = dev_type;
  key.dev_name = (gchar*)dev_name;
  key.func_id = func_id;
  e = g_tree_lookup(mapper->function_map, &key);
  if (e) {
    e->max = max;
    entry_changed(mapper, e);
    return TRUE;
  }
  return FALSE;
}
