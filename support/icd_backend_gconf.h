#ifndef ICD_BACKEND_GCONF_H
#define ICD_BACKEND_GCONF_H

#include "icd_settings_backend.h"


void icd_backend_gconf_init      (struct icd_settings *settings);

void icd_backend_gconf_delete    (icd_settings_handle handle);

icd_settings_handle
icd_backend_gconf_get_by_network (const gchar *network_type,
                                  const guint network_attrs,
                                  const gchar *network_id);

#endif
