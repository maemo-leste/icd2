
#ifndef ICD_NAME_OWNER_H
#define ICD_NAME_OWNER_H

#include <glib.h>
#include "icd_context.h"

gboolean icd_name_owner_remove_filter (const gchar *application);

gboolean icd_name_owner_add_filter    (const gchar *application);

gboolean icd_name_owner_init          (struct icd_context *icd_ctx);

#endif
