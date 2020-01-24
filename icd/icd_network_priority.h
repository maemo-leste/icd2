#ifndef ICD_NETWORK_PRIORITY_H
#define ICD_NETWORK_PRIORITY_H

#include <glib.h>
#include "icd_scan.h"

void icd_network_priority_pref_init (void);

gint icd_network_priority_get (const gchar *srv_type,
                               const gchar *srv_id,
                               const gchar *network_type,
                               const guint network_attrs);

gboolean icd_network_priority (const gchar *srv_type,
                               const gchar *srv_id,
                               const gchar *network_type,
                               const guint network_attrs,
                               gint *network_priority);

#endif
