#ifndef __C2IP_SCAN_H__GYZ861LCQA__
#define __C2IP_SCAN_H__GYZ861LCQA__

#include <glib-object.h>
#include <gio/gio.h>

#define C2IP_SCAN_ERROR (c2ip_scan_error_quark())
enum {
  C2IP_SCAN_ERROR_INVALID_REPLY = 1,
  C2IP_SCAN_ERROR_NO_RANGE
};

#define C2IP_SCAN_TYPE                  (c2ip_scan_get_type ())
#define C2IP_SCAN(obj)                  (G_TYPE_CHECK_INSTANCE_CAST ((obj), C2IP_SCAN_TYPE, C2IPScan))
#define IS_C2IP_SCAN(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), C2IP_SCAN_TYPE))
#define C2IP_SCAN_CLASS(klass)          (G_TYPE_CHECK_CLASS_CAST ((klass), C2IP_SCAN_TYPE, C2IPScanClass))
#define IS_C2IP_SCAN_CLASS(klass)       (G_TYPE_CHECK_CLASS_TYPE ((klass), C2IP_SCAN_TYPE))
#define C2IP_SCAN_GET_CLASS(obj)        (G_TYPE_INSTANCE_GET_CLASS ((obj), C2IP_SCAN_TYPE, C2IPScanClass))

typedef struct _C2IPScan C2IPScan;
typedef struct _C2IPScanClass C2IPScanClass;


C2IPScan *
c2ip_scan_new(void);

gboolean
c2ip_scan_start(C2IPScan *scanner, GInetAddressMask *range, GError **err);

void
c2ip_scan_stop(C2IPScan *scanner);

#endif /* __C2IP_SCAN_H__GYZ861LCQA__ */
