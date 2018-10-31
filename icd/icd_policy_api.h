#ifndef ICD_POLICY_API_H
#define ICD_POLICY_API_H

/**
@file icd_policy_api.h

@author patrik.flykt@nokia.com

@copyright (C) 2007 Nokia Corporation. All rights reserved.

@addtogroup icd_policy_api ICd policy API implementation

The data structure of the list of requests in icd_context and the existing
request list passed to #icd_policy_request_new_fn usable with e.g.
icd_policy_merge_requests_fn looks like the following:
<pre>
 +-icd_context-+    +-GSList-------+
 |             |---&gt;| request_list |
 +-------------+    +--------------+
                         |                    +-icd_policy_request-+
                         +----&gt;icd_request---&gt;| req                |&lt;---+
                         |                    +--------------------+    |
                         |                                              |
                         |                    +-icd_policy_request-+    |
                         +----&gt;icd_request---&gt;| req                |&lt;---+
                         |                    +--------------------+    |</pre>

<pre>                        ...                                            ...</pre>

<pre>                         |                    +-icd_policy_request-+    |
                         +----&gt;icd_request---&gt;| req                |&lt;---+
                                              +--------------------+    |
                                                                        |
                                                        +-GSList------------+
                                                        | existing_requests |
                                                        +-------------------+
 </pre>
A policy module can make a request to wait for an external event such as UI
dialog appearing by returning ICD_POLICY_WAITING. Policy processing will
stop, whereby a new request is needed to clean up the waiting request. On
cancelling a request, the request_cancel will be called for all policy
modules to clean up an pending calls or timers left behind.

@ingroup internal

 * @{ */

#include <glib.h>

#include "icd_context.h"
#include "policy_api.h"

/** Internal representation of a policy module */
struct icd_policy_module {
  /** module handle */
  gpointer handle;
  /** module name */
  gchar *name;
  /** the policy api */
  struct icd_policy_api policy;
};

/** Callback for the new_connection policy request
 * @param status          status of the policy request
 * @param policy_request  the policy request structure
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

/** @} */

#endif
