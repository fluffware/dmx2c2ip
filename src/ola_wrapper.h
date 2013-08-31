#include <glib.h>
G_BEGIN_DECLS
typedef struct _OLAWrapper OLAWrapper;

typedef void (*OLAWrapperReceived)(const guint8 *data, gsize len,
				   gpointer user_data);

OLAWrapper *
ola_wrapper_read_new(OLAWrapperReceived recv_cb, gpointer user_data,
		     guint universe, GError **err);

void
ola_wrapper_run(OLAWrapper *wrapper);

void
ola_wrapper_terminate(OLAWrapper *wrapper);

void
ola_wrapper_destroy(OLAWrapper *wrapper);
  
G_END_DECLS

