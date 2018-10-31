#ifndef ICD_SRV_PROVIDER_H
#define ICD_SRV_PROVIDER_H

/**
@addtogroup icd_srv_provider Service provider API implementation

A service provider module is set up in gconf as follows:
<pre>ICD_GCONF_SRV_PROVIDERS/&lt;srv_type&gt;/module	name of the module to load
ICD_GCONF_SRV_PROVIDERS/&lt;srv_type&gt;/network_type	list of strings containing the network types supported</pre>

@ingroup internal

 * @{ */

#include <glib.h>

#include "icd_context.h"
#include "icd_network_api.h"
#include "icd_scan.h"
#include "icd_iap.h"
#include "srv_provider_api.h"

/** Service provider module */
struct icd_srv_module {
  /** module handle */
  void *handle;
  /** name of this module */
  gchar *name;

  /** list of pids this module wants to track */
  GSList *pid_list;
  /** service api functions */
  struct icd_srv_api srv;

};

/** Service provider api callback for going through every service provider
 * module
 *
 * @param  module     the service provider module
 * @param  user_data  user data passed to icd_srv_provider_foreach_module
 * @return if TRUE the callback will be called again with the next module; if
 *         FALSE iteration is stopped
 */
typedef gboolean
(*icd_srv_provider_foreach_module_fn) (struct icd_srv_module* module,
                                       gpointer user_data);

struct icd_srv_module *
icd_srv_provider_foreach_module (struct icd_context *icd_ctx,
                                 icd_srv_provider_foreach_module_fn foreach_fn,
                                 gpointer user_data);

/** Service provider connect callback function
 * @param status     status of the connect
 * @param err_str    error string or NULL on success
 * @param user_data  user data
 */
typedef void (*icd_srv_provider_connect_cb_fn) (enum icd_srv_status status,
                                                const gchar *err_str,
                                                gpointer user_data);

/** Service provider disconnect callback function
 * @param status     status of the connect
 * @param user_data  user data
 */
typedef void (*icd_srv_provider_disconnect_cb_fn) (enum icd_srv_status status,
                                                   gpointer user_data);

gboolean icd_srv_provider_disconnect (struct icd_iap *iap,
                                      icd_srv_provider_disconnect_cb_fn cb,
                                      gpointer user_data);

gboolean icd_srv_provider_has_next (struct icd_iap *iap);

gboolean icd_srv_provider_connect (struct icd_iap *iap,
                                   icd_srv_provider_connect_cb_fn cb,
                                   gpointer user_data);

gboolean icd_srv_provider_identify (struct icd_network_module *nw_module,
                                    struct icd_scan_cache *cache_entry,
                                    enum icd_scan_status status);

gboolean icd_srv_provider_notify_pid (struct icd_context *icd_ctx,
                                      const pid_t pid,
                                      const gint exit_value);

gboolean icd_srv_provider_load_modules (struct icd_context *icd_ctx);

void icd_srv_provider_unload_modules (struct icd_context *icd_ctx);

gboolean icd_srv_provider_check (const gchar *network_type);

/** @} */

#endif
