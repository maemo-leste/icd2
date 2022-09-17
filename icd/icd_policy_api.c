/**
@file icd_policy_api.c
@copyright GNU GPLv2 or later

@addtogroup icd_policy_api ICd policy API implementation
@ingroup internal

 * @{ */

#include <gconf/gconf.h>
#include "icd_policy_api.h"
#include "policy_api.h"
#include "icd_plugin.h"
#include "icd_log.h"
#include "icd_request.h"
#include "icd_iap.h"
#include "icd_scan.h"
#include "network_api.h"
#include "icd_type_modules.h"
#include "icd_network_priority.h"
#include "icd_srv_provider.h"

/** prefix for the ICd policy API modules */
#define ICD_POLICY_API_PREFIX   "libicd_policy_"

/** name of the policy API init function */
#define ICD_POLICY_INIT   "icd_policy_init"

/** data needed by the new_request policy */
struct icd_policy_api_request_data {
  /** callback to call when request status is known */
  icd_policy_api_request_cb_fn cb;

  /** callback user data */
  gpointer user_data;

  /** list of existing requests */
  GSList *existing_requests;
};

/** policy module scan callback and user data */
struct icd_policy_scan_data {
  /** policy module scan callback */
  icd_policy_scan_cb_fn cb;

  /** policy module user data */
  gpointer user_data;
};

struct icd_policy_api_async_data;

/**
 * Function prototype for calling the actual asynchronous policy function
 *
 * @param module      the policy module
 * @param request     the requested connection
 * @param async_data  data for the asynchronous function call
 *
 * @return  TRUE if the module has a policy function that will cause the
 *          callback to be called; FALSE if no policy function is called
 */
typedef gboolean(* icd_policy_api_async_call_fn)(
    struct icd_policy_module *module, struct icd_policy_request *request,
    struct icd_policy_api_async_data *async_data);

/** asynchronous policy module function data */
struct icd_policy_api_async_data {
  /** callback to call with final policy decision */
  icd_policy_api_async_call_fn call_policy;

  /** policy result callback data */
  gpointer user_data;

  /** current policy module in list */
  GSList *module_list;
};

/**
 * Policy module function that will be called once for each policy module
 * until #ICD_POLICY_REJECTED is returned
 *
 * @param module     the policy module
 * @param request    the request to apply policy to
 * @param user_data  user data to pass to the module
 *
 * @return  #ICD_POLICY_REJECTED if the policy got rejected,
 *          #ICD_POLICY_ACCEPTED otherwise
 */
typedef enum icd_policy_status(* icd_policy_api_run_module_fn)(
    struct icd_policy_module *module, struct icd_policy_request *request,
    gpointer user_data);

const gchar const* icd_policy_api_state[] = {
  "ICD_POLICY_ACCEPTED",
  "ICD_POLICY_MERGED",
  "ICD_POLICY_WAITING",
  "ICD_POLICY_REJECTED"
};

static GSList *scan_list = NULL;

/**
 * Module iterator for the 'cancel_request' function.
 *
 * @param module     the policy module
 * @param request    the request to cancel
 * @param user_data  user data; not used
 *
 * @return  #ICD_POLICY_ACCEPTED to iterate through all policy modules
 */
static enum icd_policy_status
icd_policy_api_request_cancel_iter(struct icd_policy_module *module,
                                   struct icd_policy_request *request,
                                   gpointer user_data)
{
  if (module->policy.cancel_request)
  {
    ILOG_INFO("module '%s' cancel function called for request %p", module->name,
              request);

    module->policy.cancel_request(request, &module->policy.private);
  }

  return ICD_POLICY_ACCEPTED;
}

/**
 * Iterate over all the modules and call the policy callback function for
 * each of them.
 *
 * @param policy_fn  the policy callback function call for each module
 * @param request    the request to apply policy to
 * @param user_data  user data to pass to the function
 *
 * @return  enum #icd_policy_status
 */
static enum icd_policy_status
icd_policy_api_run(icd_policy_api_run_module_fn policy_fn,
                   struct icd_policy_request *request, gpointer user_data)
{
  struct icd_context *icd_ctx = icd_context_get();
  GSList *l;

  if (!icd_ctx || !policy_fn)
  {
    ILOG_ERR("icd_ctx or foreach_fn cannot be NULL");
    return ICD_POLICY_REJECTED;

  }

  for (l = icd_ctx->policy_module_list; l; l = l->next)
  {
    struct icd_policy_module *pm = (struct icd_policy_module *)l->data;

    if (pm)
    {
      if (policy_fn(pm, request, user_data) == ICD_POLICY_REJECTED)
        return ICD_POLICY_REJECTED;
    }
    else
      ILOG_WARN("module list has NULL module data");
  }

  return ICD_POLICY_ACCEPTED;
}

/**
 * Cancel a request that is in ICD_POLICY_WAITING state by informing each
 * policy module.
 *
 * @param req  the policy request structure
 */
void
icd_policy_api_request_cancel(struct icd_policy_request *req)
{
  struct icd_request *request = (struct icd_request *)req->request_token;

  if (request->state == ICD_REQUEST_WAITING ||
      request->state == ICD_REQUEST_POLICY_PENDING)
  {
    icd_policy_api_run(icd_policy_api_request_cancel_iter, req, NULL);
  }
  else
  {
    ILOG_ERR("Request %p is not in ICD_REQUEST_WAITING or ICD_REQUEST_POLICY_PENDING state",
             request);
  }
}

/**
 * Go through all connections and add them to the list in reverse order
 *
 * @param iap        current iap
 * @param user_data  connection list
 *
 * @return  TRUE to iterate over all connections
 */
static gboolean
icd_policy_api_existing_conn_foreach(struct icd_iap *iap, gpointer user_data)
{
  GSList **l = (GSList **)user_data;

  *l = g_slist_prepend(*l, &iap->connection);

  return TRUE;
}

/**
 * Get the list of existing connections
 * @return  list of connections; the caller needs to free only the GSList.
 */
static GSList *
icd_policy_api_existing_conn_get(void)
{
  GSList *l = NULL;

  icd_iap_foreach(icd_policy_api_existing_conn_foreach, &l);

  return l;
}

/**
 * Iterator for the disconnect policy
 *
 * @param module     the policy module
 * @param request    the request to apply policy to
 * @param user_data  reference count
 */
static enum icd_policy_status
icd_policy_api_iap_disconnect_iter(struct icd_policy_module *module,
                                   struct icd_policy_request *request,
                                   gpointer user_data)
{
  enum icd_policy_status rv;
  GSList *l;

  if (!module->policy.disconnect)
    return ICD_POLICY_ACCEPTED;

  ILOG_INFO("running module '%s' disconnect policy", module->name);

  l = icd_policy_api_existing_conn_get();
  rv = module->policy.disconnect(request, GPOINTER_TO_INT(user_data), l,
                                 &module->policy.private);
  g_slist_free(l);

  return rv;
}

/**
 * Disconnect policy called when ICd attempts to disconnect from a network
 *
 * @param connection  the connection that is to be tried
 * @param refcount    a reference count on the number of applications using
 *                    this connection or -1 if disconnect forced by
 *                    Connectivity UI
 */
enum icd_policy_status
icd_policy_api_iap_disconnect(struct icd_policy_request *connection,
                              gint refcount)
{
  return icd_policy_api_run(icd_policy_api_iap_disconnect_iter, connection,
                            GINT_TO_POINTER(refcount));
}

/**
 * Create the list of existing requests, new_request is omitted from the list
 * @return  list of requests; the caller needs to free only the GSList.
 */
static GSList *
icd_policy_api_existing_requests_get(struct icd_request *new_request)
{
  GSList *l;
  GSList *rv = NULL;

  for (l = icd_context_get()->request_list; l; l = l->next)
  {
    struct icd_request *request = (struct icd_request *)l->data;

    if (request && request != new_request &&
        request->state != ICD_REQUEST_DISCONNECTED &&
        request->state != ICD_REQUEST_DENIED )
    {
      rv = g_slist_prepend(rv, &request->req);

      if (request->try_iaps)
      {
        struct icd_iap *iap = (struct icd_iap *)request->try_iaps->data;

        if (iap)
          rv = g_slist_prepend(rv, &iap->connection);
      }
    }
  }

  return rv;
}

/**
 * Call an asynchronous policy module function
 *
 * @param req         the request
 * @param async_data  policy data
 *
 * @return  TRUE if a module was called; FALSE if there were no more modules
 *          to call
 */
static gboolean
icd_policy_api_run_async_next(struct icd_policy_request *req,
                              struct icd_policy_api_async_data *async_data)
{
  if (!async_data->module_list)
    return FALSE;

  while (async_data->module_list)
  {
    struct icd_policy_module *module =
        (struct icd_policy_module *)async_data->module_list->data;

    async_data->module_list = async_data->module_list->next;

    if (async_data->call_policy(module, req, async_data))
      return TRUE;
  }

  return FALSE;
}

/**
 * Free async policy api data
 * @param data  the data to free
 */
static void
icd_policy_api_async_data_free(struct icd_policy_api_async_data *data)
{
  g_free(data);
}

/**
 * Status callback for the new request policy function
 *
 * @param status        status of the new request function
 * @param req           the policy request
 * @param policy_token  data for the async policy function call
 */
static void
icd_policy_api_request_cb(enum icd_policy_status status,
                          struct icd_policy_request *req,
                          gpointer policy_token)
{
  struct icd_policy_api_async_data *async_data =
      (struct icd_policy_api_async_data *)policy_token;
  struct icd_policy_api_request_data *request_data = async_data->user_data;

  g_slist_free(request_data->existing_requests);
  request_data->existing_requests = NULL;

  if (status != ICD_POLICY_ACCEPTED)
  {
    ILOG_DEBUG("policy returned %s for request %p",
               icd_policy_api_state[status], req ? req->request_token : NULL);

    if (status != ICD_POLICY_MERGED )
      request_data->cb(status, req);

    g_free(request_data);
    icd_policy_api_async_data_free(async_data);
  }
  else
  {
    request_data->existing_requests = icd_policy_api_existing_requests_get(
          (struct icd_request *)req->request_token);

    if (!icd_policy_api_run_async_next(req, async_data))
    {
      ILOG_INFO("all new request policies done");

      request_data->cb(ICD_POLICY_ACCEPTED, req);
      g_slist_free(request_data->existing_requests);
      g_free(request_data);
      icd_policy_api_async_data_free(async_data);
    }
  }
}

/**
 * Function that calls the new_request policy module function
 *
 * @param module      the policy module
 * @param request     the requested network
 * @param async_data  policy structure to pass as the policy_token to the
 *                    policy module funcion
 *
 * @return  TRUE if the module has a policy function that will cause the
 *          callback to be called; FALSE if no policy function is called
 */
static gboolean
icd_policy_api_request_call(struct icd_policy_module *module,
                            struct icd_policy_request *request,
                            struct icd_policy_api_async_data *async_data)
{
  struct icd_policy_api_request_data *request_data =
      (struct icd_policy_api_request_data *)async_data->user_data;

  if (module->policy.new_request)
  {
    ILOG_INFO("running module '%s' new_request policy", module->name);

    module->policy.new_request(request, request_data->existing_requests,
                               icd_policy_api_request_cb, async_data,
                               &module->policy.private);
    return TRUE;
  }

  return FALSE;
}

/**
 * Initialize the asynchronous policy module function data and start running
 * the policy modules one by one
 *
 * @param req          the request
 * @param call_policy  the function that calls the policy module function
 *                     with correct parameters
 * @param user_data    user data for the function
 *
 * @return  TRUE if an asynchronous policy module function got called, FALSE
 *          if no policy module contained any suitable functions
 */
static gboolean
icd_policy_api_run_async(struct icd_policy_request *req,
                         icd_policy_api_async_call_fn call_policy,
                         gpointer  user_data)
{
  struct icd_policy_api_async_data *async_data =
      g_new0(struct icd_policy_api_async_data, 1);
  struct icd_context *icd_ctx = icd_context_get();

  async_data->call_policy = call_policy;
  async_data->user_data = user_data;
  async_data->module_list = icd_ctx->policy_module_list;

  if (icd_policy_api_run_async_next(req, async_data))
    return TRUE;

  ILOG_DEBUG("no policy modules can be run");
  g_free(async_data);

  return FALSE;
}

/**
 * New request policy
 *
 * @param req        the new request
 * @param cb         the callback to call with the policy decision
 * @param user_data  user data to pass to the callback
 */
void
icd_policy_api_new_request(struct icd_policy_request *req,
                           icd_policy_api_request_cb_fn cb,
                           gpointer user_data)
{
  gboolean async_success;
  struct icd_policy_api_request_data *request_data =
             g_new0(struct icd_policy_api_request_data, 1);

  request_data->cb = cb;
  request_data->user_data = user_data;
  request_data->existing_requests =
      icd_policy_api_existing_requests_get
      ((struct icd_request *)req->request_token);

  async_success = icd_policy_api_run_async(req,
                      icd_policy_api_request_call, request_data);

  if (!async_success)
  {
    ILOG_INFO("no module had a new_request policy");

    request_data->cb(ICD_POLICY_ACCEPTED, req);
    g_slist_free(request_data->existing_requests);
    g_free(request_data);
  }
}

/**
 * Iterator for the connect policy
 *
 * @param module     the policy module
 * @param request    the request to apply policy to
 * @param user_data  not used
 */
static enum icd_policy_status
icd_policy_api_iap_connect_iter(struct icd_policy_module *module,
                                struct icd_policy_request *request,
                                gpointer user_data)
{
  enum icd_policy_status rv;
  GSList *l;

  if (!module->policy.connect)
    return ICD_POLICY_ACCEPTED;

  ILOG_INFO("running module '%s' connect policy", module->name);

  l = icd_policy_api_existing_conn_get();
  rv = module->policy.connect(request, l, &module->policy.private);
  g_slist_free(l);

  return rv;
}

/**
 * Connect policy called when ICd attempts to connect to a network
 * @param connection  the connection that is to be tried
 */
enum icd_policy_status
icd_policy_api_iap_connect(struct icd_policy_request *connection)
{
  return icd_policy_api_run(icd_policy_api_iap_connect_iter, connection, NULL);
}

/**
 * Add a network connection to try in response to the policy decision. Any
 * policy module using this function must be compatible with the
 * corresponding network module in order to set correct *_type and *_id.
 *
 * @param req            the request to which the new network is added
 * @param service_type   service provider type, see srv_provider_api.h
 * @param service_attrs  service provider attributes, see srv_provider_api.h
 * @param service_id     service_provider id, see srv_provider_api.h
 * @param network_type   network type, see network_api.h
 * @param network_attrs  network attributes, see network_api.h
 * @param network_id     network id, see network_api.h
 */
static void
icd_policy_api_add_iap(struct icd_policy_request *req, gchar *service_type,
                       guint service_attrs, gchar *service_id,
                       gchar *network_type, guint network_attrs,
                       gchar *network_id, gint network_priority)
{
  icd_request_add_iap((struct icd_request *)req->request_token, service_type,
                      service_attrs, service_id, network_type, network_attrs,
                      network_id, network_priority);
}

/**
 * Iterator for the disconnected informational policy
 *
 * @param module     the policy module
 * @param request    the request to apply policy to
 * @param user_data  reason for disconnect; NULL on success
 *
 * @return  ICD_POLICY_ACCEPTED
 */
static enum icd_policy_status
icd_policy_api_iap_disconnected_iter(struct icd_policy_module *module,
                                     struct icd_policy_request *request,
                                     gpointer user_data)
{
  GSList *l;

  if (module->policy.disconnected)
  {
    ILOG_INFO("running module '%s' disconnected policy", module->name);

    l = icd_policy_api_existing_conn_get();
    module->policy.disconnected(request, (const gchar *)user_data, l,
                                &module->policy.private);
    g_slist_free(l);
  }

  return ICD_POLICY_ACCEPTED;
}

/**
 * Informational policy to call when a network connection has been
 * disconnected
 *
 * @param connection  the connection that was disconnected
 * @param err_str     reason for disconnect; NULL on success
 */
void
icd_policy_api_iap_disconnected(struct icd_policy_request *connection,
                                const gchar *err_str)
{
  icd_policy_api_run(icd_policy_api_iap_disconnected_iter, connection,
                     (gpointer)err_str);
}

/**
 * Iterator for the connection succeeded informational policy
 *
 * @param module     the policy module
 * @param request    the request to apply policy to
 * @param user_data  not used
 *
 * @return  ICD_POLICY_ACCEPTED
 */
static enum icd_policy_status
icd_policy_api_iap_succeeded_iter(struct icd_policy_module *module,
                                  struct icd_policy_request *request,
                                  gpointer user_data)
{
  GSList *l;

  if (module->policy.connected)
  {
    ILOG_INFO("running module '%s' connected policy", module->name);
    l = icd_policy_api_existing_conn_get();
    module->policy.connected(request, l, &module->policy.private);
    g_slist_free(l);
  }

  return ICD_POLICY_ACCEPTED;
}

/**
 * Informational policy to call when a network connection has been
 * established
 *
 * @param connection  the connection that was connected
 */
void
icd_policy_api_iap_succeeded(struct icd_policy_request *connection)
{
  icd_policy_api_run(icd_policy_api_iap_succeeded_iter, connection, NULL);
}

/**
 * Get the callback and user data list
 * @return  the list
 */
static GSList **icd_policy_api_scan_list_get(void)
{
  return &scan_list;
}

/**
 * Iterator for the restart policy
 *
 * @param module     the policy module
 * @param request    the request to apply policy to
 * @param user_data  user data to pass to the module
 */
static enum icd_policy_status
icd_policy_api_iap_restart_iter(struct icd_policy_module *module,
                                struct icd_policy_request *request,
                                gpointer user_data)
{
  if (module->policy.restart)
  {
    ILOG_INFO("running module '%s' restart policy", module->name);
    return module->policy.restart(request, GPOINTER_TO_UINT(user_data),
                                  &module->policy.private);
  }

  return ICD_POLICY_ACCEPTED;
}

/**
 * Restart policy
 *
 * @param connection     the IAP network connection that is restarting
 * @param restart_count  restart count
 *
 * @return  #ICD_POLICY_REJECTED if the limit has been exceded,
 *          #ICD_POLICY_ACCEPTED otherwise
 */
enum icd_policy_status
icd_policy_api_iap_restart(struct icd_policy_request *connection,
                           guint restart_count)
{
  return icd_policy_api_run(icd_policy_api_iap_restart_iter, connection,
                            GUINT_TO_POINTER(restart_count));
}

/**
 * Scan results
 *
 * @param status        status of this network
 * @param srv_provider  service provider entry; guaranteed to exist only for
 *                      the lifetime of this callback function
 * @param cache_entry   scan results; guaranteed to exist only for the
 *                      lifetime of this callback function
 * @param user_data     used data given to the scan callback
 */
static void
icd_policy_api_scan_result(enum icd_scan_status status,
                           const struct icd_scan_srv_provider *srv_provider,
                           const struct icd_scan_cache *cache_entry,
                           gpointer user_data)
{
  struct icd_policy_scan_data *data = (struct icd_policy_scan_data *)user_data;
  enum icd_policy_scan_status scan_status;

  switch (status)
  {
    case ICD_SCAN_NEW:
      scan_status = ICD_POLICY_SCAN_NEW_NETWORK;
      break;
    case ICD_SCAN_UPDATE:
      scan_status = ICD_POLICY_SCAN_UPDATE_NETWORK;
      break;
    case ICD_SCAN_NOTIFY:
      return;
    case ICD_SCAN_EXPIRE:
      scan_status = ICD_POLICY_SCAN_EXPIRED_NETWORK;
      break;
    case ICD_SCAN_COMPLETE:
      scan_status = ICD_POLICY_SCAN_DONE;
      break;
    default:
      ILOG_WARN("scan status %d not supported by policy functions", status);
      return;
  }

  if (srv_provider)
  {
    data->cb(scan_status, srv_provider->service_name,
             srv_provider->service_type, srv_provider->service_attrs,
             srv_provider->service_id, srv_provider->service_priority,
             cache_entry->network_name, cache_entry->network_type,
             cache_entry->network_attrs, cache_entry->network_id,
             cache_entry->network_priority, cache_entry->signal,
             data->user_data);
  }
  else
  {
    data->cb(scan_status, NULL, NULL, 0, NULL, 0, cache_entry->network_name,
             cache_entry->network_type, cache_entry->network_attrs,
             cache_entry->network_id, cache_entry->network_priority,
             cache_entry->signal, data->user_data);
  }
}

/**
 * Function for a module to request a network scan.
 * icd_policy_api_scan_stop() has to be called as many times as this function
 * is called even if the same callback and user_data pair are added.
 *
 * @param type       network type
 * @param scope      scan scope
 * @param cb         callback function to call with scan results
 * @param user_data  user data to pass to the callback function
 */
static void
icd_policy_api_scan_start(const gchar *type, const guint scope,
                          icd_policy_scan_cb_fn cb, gpointer user_data)
{
  GSList **scan_list;
  struct icd_policy_scan_data *data;

  if (!cb)
  {
    ILOG_CRIT("policy api scan callback cannot be NULL");
    return;
  }

  scan_list = icd_policy_api_scan_list_get();
  data = g_new0(struct icd_policy_scan_data, 1);
  data->cb = cb;
  data->user_data = user_data;
  *scan_list = g_slist_prepend(*scan_list, data);

  if (!icd_scan_results_request(type, scope, icd_policy_api_scan_result, data))
  {
    ILOG_DEBUG("policy api scan did not find anything to scan, freeing...");

    *scan_list = g_slist_remove(*scan_list, data);
    data->cb(ICD_POLICY_SCAN_DONE, 0, 0, 0, 0, 0, 0, type, 0, 0, 0, 0,
             data->user_data);
    g_free(data);
  }

}

/**
 * Find callback and user data
 * @param cb         callback
 * @param user_data  user_data
 */
static struct icd_policy_scan_data *
icd_policy_api_scan_find(icd_policy_scan_cb_fn cb, gpointer user_data)
{
  GSList *l;

  for (l = *icd_policy_api_scan_list_get(); l; l = l->next)
  {
    struct icd_policy_scan_data *data =
        (struct icd_policy_scan_data *)l->data;

    if (data)
    {
      if (cb == data->cb && user_data == data->user_data)
        return data;
    }
  }

  ILOG_ERR("policy api could not find scan cb %p with user data %p", cb,
           user_data);

  return NULL;
}

/**
 * Stop returning scan results for the given callback and user data
 * @param cb         scan callback
 * @param user_data  user_data given in icd_policy_api_scan_start()
 */
static void
icd_policy_api_scan_stop(icd_policy_scan_cb_fn cb, gpointer user_data)
{
  struct icd_policy_scan_data *data = icd_policy_api_scan_find(cb, user_data);

  if (data)
  {
    GSList **scan_list;

    icd_scan_results_unregister(icd_policy_api_scan_result, data);
    scan_list = icd_policy_api_scan_list_get();
    *scan_list = g_slist_remove(*scan_list, data);
    g_free(data);
  }
}

/**
 * Merge two request together and free the request_to_merge structure.
 *
 * @param request_to_merge  the request that will be joined with the existing
 *                          one. The structure will also be freed and may
 *                          point to anything after that.
 * @param existing_request  the request that continues to exists after a
 *                          merge
 */
static void
icd_policy_api_merge_requests(struct icd_policy_request *request_to_merge,
                              struct icd_policy_request *existing_request)
{
  if (request_to_merge)
  {
    if (existing_request)
    {
      struct icd_request *merge =
          (struct icd_request *)request_to_merge->request_token;
      struct icd_request *existing =
          (struct icd_request *)existing_request->request_token;

      if (request_to_merge->request_token == existing )
      {
        ILOG_CRIT("request to merge %p is the same as existing request %p",
                  request_to_merge, existing_request);
      }
      else
        icd_request_merge(merge, existing);
    }
    else
      ILOG_CRIT("NULL pointer passed instead of existing request to merge");
  }
  else
    ILOG_CRIT("NULL pointer passed instead of request to merge");
}

/**
 * Function to create a new request
 *
 * @param policy_attrs   ICD_POLICY_ATTRIBUTE_* attributes
 * @param service_type   service provider type, see srv_provider_api.h
 * @param service_attrs  service provider attributes, see srv_provider_api.h
 * @param service_id     service_provider id, see srv_provider_api.h
 * @param network_type   network type, see network_api.h
 * @param network_attrs  network attributes, see network_api.h
 * @param network_id     network id, see network_api.h
 */
static void
icd_policy_api_make_request(guint policy_attrs, gchar *service_type,
                            guint service_attrs, gchar *service_id,
                            gchar *network_type, guint network_attrs,
                            gchar *network_id)
{
  icd_request_make(icd_request_new(policy_attrs, service_type, service_attrs,
                                   service_id, network_type, network_attrs,
                                   network_id));
}

/**
 * Disconnect an active IAP
 * @param network  the network to disconnect
 */
static void
icd_policy_api_disconnect_iap(struct icd_policy_request *network)
{
  if (network)
  {
    struct icd_request *request = (struct icd_request *)network->request_token;

    if (&request->req == network)
    {
      ILOG_CRIT("it is NOT ok to give a request '%p' instead of a network",
                &request->req);
    }
    else
      icd_request_cancel(network->request_token, ICD_POLICY_ATTRIBUTE_CONN_UI);
  }
  else
    ILOG_CRIT("iap to disconnect is NULL");
}

/**
 * Initialize the loaded module
 *
 * @param module_name    module filename without path
 * @param handle         module handle; used for unloading
 * @param init_function  module init function
 * @param data           icd context
 *
 * @return  TRUE on success, FALSE on failure
 * @todo  make init return TRUE on success and FALSE on failure whereby the
 *        module is unloaded
 */
static gboolean
icd_policy_api_init_cb(const gchar *module_name, void *handle,
                       gpointer init_function, gpointer data)
{
  struct icd_context *icd_ctx = (struct icd_context *)data;
  struct icd_policy_module *module = g_new0(struct icd_policy_module, 1);

  module->handle = handle;
  ((icd_policy_init_fn)init_function)(&module->policy,
                                      icd_policy_api_add_iap,
                                      icd_policy_api_merge_requests,
                                      icd_policy_api_make_request,
                                      icd_policy_api_scan_start,
                                      icd_policy_api_scan_stop,
                                      icd_policy_api_disconnect_iap,
                                      icd_network_priority,
                                      icd_srv_provider_check);
  module->name = g_strdup(module_name);
  icd_ctx->policy_module_list = g_slist_append(icd_ctx->policy_module_list,
                                               module);

  return TRUE;
}

/**
 * Load all policy API modules
 * @param icd_ctx  icd context
 * @return  the status from icd_plugin_load_all
 */
gboolean
icd_policy_api_load_modules(struct icd_context *icd_ctx)
{
  GSList *modules = icd_policy_modules_get();
  gboolean rv;

  for (rv = icd_plugin_load_list("/usr/lib/icd2", modules, ICD_POLICY_INIT,
                                 icd_policy_api_init_cb, icd_ctx);
        modules; modules = g_slist_delete_link(modules, modules))
  {
    g_free(modules->data);
  }

  return rv;
}

/**
 * Unload all policy modules
 * @param icd_ctx  icd context
 */
void
icd_policy_api_unload_modules(struct icd_context *icd_ctx)
{
  GSList *l = icd_ctx->policy_module_list;

  ILOG_INFO("unloading policy api modules");

  while(l)
  {
    struct icd_policy_module *module = (struct icd_policy_module *)l->data;
    GSList *next = l->next;

    if (module->policy.destruct)
    {
      ILOG_INFO("calling policy_destruct of module %s", module->name);
      module->policy.destruct(&module->policy.private);
    }

    icd_plugin_unload_module(module->handle);
    g_free(module->name);
    g_free(module);
    icd_ctx->policy_module_list =
        g_slist_remove_link(icd_ctx->policy_module_list, l);
    l = next;
  }
}

/** @} */
