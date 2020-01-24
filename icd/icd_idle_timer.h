#ifndef ICD_IDLE_TIMER_H
#define ICD_IDLE_TIMER_H

#include <glib.h>

#include "icd_iap.h"
#include "icd_context.h"

gboolean icd_idle_timer_set    (struct icd_iap *iap);
gboolean icd_idle_timer_unset  (struct icd_iap *iap);
gboolean icd_idle_timer_init   (struct icd_context *icd_ctx);
void     icd_idle_timer_remove (struct icd_context *icd_ctx);

#endif
