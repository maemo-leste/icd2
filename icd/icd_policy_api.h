#ifndef ICD_POLICY_API_H
#define ICD_POLICY_API_H

#include <glib.h>

#include "icd_context.h"
#include "policy_api.h"

struct icd_policy_module {
  gpointer handle;
  gchar *name;
  struct icd_policy_api policy;
};

/**
 * @brief  Callback for the new_connection policy request
 *
 * @param  status          status of the policy request
 * @param  policy_request  the policy request structure
 */
typedef void
(*icd_policy_api_request_cb_fn) (enum icd_policy_status status,
                                 struct icd_policy_request *req);

void icd_policy_api_new_request (struct icd_policy_request *request,
                                 icd_policy_api_request_cb_fn cb,
                                 gpointer user_data);
void icd_policy_api_request_cancel (struct icd_policy_request *req);

enum icd_policy_status
icd_policy_api_iap_connect (struct icd_policy_request *req);
enum icd_policy_status
icd_policy_api_iap_restart (struct icd_policy_request *request,
                            guint restart_count);
void icd_policy_api_iap_succeeded (struct icd_policy_request *req);
enum icd_policy_status
icd_policy_api_iap_disconnect (struct icd_policy_request *connection,
                               gint refcount);
void icd_policy_api_iap_disconnected (struct icd_policy_request *req,
                                      const gchar *err_str);

void icd_policy_api_scan_stop_status (const gchar *network_type);
void icd_policy_api_scan_start_status (const gchar *network_type);

gboolean icd_policy_api_load_modules (struct icd_context *icd_ctx);
void icd_policy_api_unload_modules (struct icd_context *icd_ctx);

#endif
