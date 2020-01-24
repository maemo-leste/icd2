#ifndef ICD_OSSO_IC_H
#define ICD_OSSO_IC_H

/**
@file icd_osso_ic.h
@copyright GNU GPLv2 or later

@addtogroup icd_osso_ic Compatibility functions providing OSSO IC D-Bus API
@ingroup internal

 * @{ */

#include <glib.h>
#include "icd_context.h"

/**
 * Callback function called when a UI retry or save request has completed
 * @param success    TRUE on success, FALSE on failure
 * @param user_data  user data passed to retry or save function
 */
typedef void (*icd_osso_ui_cb_fn) (gboolean success, gpointer user_data) ;

void icd_osso_ic_send_ack         (GSList *tracking_list,
                                   const gchar *iap_name);

void icd_osso_ic_send_nack        (GSList *tracking_list);

void icd_osso_ui_send_retry       (const gchar *iap_name, const gchar *error,
                                   icd_osso_ui_cb_fn cb,
                                   gpointer user_data);

void icd_osso_ui_send_save_cancel (gpointer send_save_token);

gpointer icd_osso_ui_send_save    (const gchar *iap_name,
                                   icd_osso_ui_cb_fn cb,
                                   gpointer user_data);

gboolean icd_osso_ic_init         (struct icd_context *icd_ctx);

void icd_osso_ic_deinit           (void);

/** @} */

#endif
