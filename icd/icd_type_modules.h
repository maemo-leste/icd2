#ifndef ICD_TYPE_MODULES_H
#define ICD_TYPE_MODULES_H

#include <glib.h>
#include <osso-ic-gconf.h>

GSList *icd_network_modules_get (const gchar *network_type);
GSList *icd_policy_modules_get  (void);

#endif
