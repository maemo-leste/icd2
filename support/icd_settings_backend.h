#ifndef ICD_SETTINGS_BACKEND_H
#define ICD_SETTINGS_BACKEND_H

#include "icd_settings.h"

/** settings data; for internal use only */
struct icd_settings {
  /** list of settings handles hashed by network id */
  GHashTable *network_id;
};

gboolean icd_settings_add_handle (struct icd_settings *settings,
                                  icd_settings_handle handle);

#endif
