/**
@file policy_any.c
@copyright GNU GPLv2 or later

@addtogroup policy_any Any connection policy
@ingroup policy

 * @{ */

#include <gconf/gconf-client.h>
#include <osso-ic-gconf.h>
#include <osso-ic.h>

#include <string.h>

#include "policy_api.h"
#include "icd_log.h"


#define CHANGE_WHILE_CONNECTED_KEY ICD_GCONF_NETWORK_MAPPING \
                                         "/change_while_connected"

#define AUTO_CONNECT_KEY ICD_GCONF_NETWORK_MAPPING "/auto_connect"


/** data for the any policy */
struct policy_any_data {
  /** function to add a new network */
  icd_policy_nw_add_fn add_network;
  /** merge with an existing connection */
  icd_policy_request_merge_fn merge;

  /** function to start scanning */
  icd_policy_scan_start_fn scan_start;
  /** function to stop scanning */
  icd_policy_scan_stop_fn scan_stop;

  /** the request that we scan for */
  struct icd_policy_request *request;
  /** list of ongoing scans containing policy_scan_data structures */
  GSList *ongoing_scans;
  /** list of network types to scan */
  GSList *scan_types_list;
  /** minimum network priority desired */
  gint min_prio;
  /** whether any iaps were added to the request */
  gboolean iaps_added;
  /** list of found networks */
  GSList *found_networks;
  /** function to check if there is a service module for a given network type */
  icd_policy_service_module_check_fn srv_check;
};

/** data for keeping track of the request we're scanning for */
struct policy_scan_data {
  /** the callback for this request */
  icd_policy_request_new_cb_fn policy_done_cb;
  /** associated policy token */
  gpointer policy_token;
  /** backpointer to the module data */
  struct policy_any_data *any_data;
};

/** data about a found network */
struct policy_any_network {
  /** service type */
  gchar *service_type;
  /** service attributes */
  guint service_attrs;
  /** service id */
  gchar *service_id;

  /** network type */
  gchar *network_type;
  /** network attributes */
  guint network_attrs;
  /** network id */
  gchar *network_id;
  /** network priority */
  gint network_priority;

  /** signal level */
  enum icd_nw_levels signal;
};


/**
 * Helper function for comparing two strings where a NULL string is equal to
 * another NULL string
 *
 * @param a  string A
 * @param b  string B
 *
 * @return   TRUE if equal, FALSE if unequal
 */
static gboolean
policy_any_string_equal(const gchar *a, const gchar *b)
{
  if (!a)
    return !b;

  if (b)
    return !strcmp(a, b);

  return FALSE;
}

/**
 * Sort new network according to priority and signal strength
 *
 * @param a  network A
 * @param b  network B
 *
 * @return   < 0 if A comes before B, 0 on equality, > 0 if A comes after B
 */
static gint
policy_any_sort_network(gconstpointer a, gconstpointer b)
{
  struct policy_any_network *a_net = (struct policy_any_network *)a;
  struct policy_any_network *b_net = (struct policy_any_network *)b;
  gint rv = b_net->network_priority - a_net->network_priority;

  if (rv == 0)
    rv = b_net->signal - a_net->signal;

  return rv;
}

/**
 * Callback function for network scan.
 *
 * @param status            network scan status with ICD_POLICY_SCAN_* values
 * @param service_name      service provider name, see srv_provider_api.h
 * @param service_type      service provider type, see srv_provider_api.h
 * @param service_attrs     service provider attributes, see
 *                          srv_provider_api.h
 * @param service_id        service_provider id, see srv_provider_api.h
 * @param service_priority  service priority within a service_type
 * @param network_name      network name, see network_api.h
 * @param network_type      network type, see network_api.h
 * @param network_attrs     network attributes, see network_api.h
 * @param network_id        network id, see network_api.h
 * @param network_priority  network priority between different network_type
 * @param signal            signal level, see network_api.h
 * @param user_data         user data passed to #icd_policy_scan_start_fn
 *
 * @todo  actually we should wait until all networks have completed one round
 *        of scanning and only then remove all scan requests
 */
static void
policy_any_scan_cb(const guint status, const gchar *service_name,
                   const gchar *service_type, const guint service_attrs,
                   const gchar *service_id, gint service_priority,
                   const gchar *network_name, const gchar *network_type,
                   const guint network_attrs, const gchar *network_id,
                   gint network_priority, const enum icd_nw_levels signal,
                   gpointer user_data)
{
  struct policy_scan_data *scan_data = (struct policy_scan_data *)user_data;
  struct policy_any_data *data = scan_data->any_data;
  GSList *l;

  if (status == ICD_POLICY_SCAN_DONE)
  {
    ILOG_DEBUG("any connection scan complete for '%s', scan data %p",
               network_type, scan_data);
    scan_data->any_data->scan_stop(policy_any_scan_cb, scan_data);
    data->ongoing_scans = g_slist_remove(data->ongoing_scans, scan_data);

    if (!data->ongoing_scans && !data->scan_types_list)
    {
      if (data->iaps_added)
      {
        ILOG_INFO("any connection found iaps");

        for (l = data->found_networks; l; l = l->next)
        {
          struct policy_any_network *network =
                                    (struct policy_any_network *)l->data;

          if (network)
          {
            if (data->request)
            {
              data->add_network(data->request, network->service_type,
                                network->service_attrs, network->service_id,
                                network->network_type, network->network_attrs,
                                network->network_id, network->network_priority);
            }
            else
              ILOG_DEBUG("request disappeared %p", scan_data->any_data);

            g_free(network->service_type);
            g_free(network->service_id);
            g_free(network->network_type);
            g_free(network->network_id);
            g_free(network);
          }
          else
            ILOG_ERR("any connection network is NULL");
        }

        g_slist_free(data->found_networks);
        data->found_networks = NULL;
      }
      else
        ILOG_INFO("any connection did not find any iaps");

      if (data->request)
      {
        if (data->iaps_added)
          data->request->attrs |= ICD_POLICY_ATTRIBUTE_HAS_CONNECTIONS;
        else
          data->request->attrs |= (ICD_POLICY_ATTRIBUTE_HAS_CONNECTIONS |
                                   ICD_POLICY_ATTRIBUTE_CONNECTIONS_FAILED);
      }

      data->iaps_added = FALSE;
      scan_data->policy_done_cb(ICD_POLICY_ACCEPTED, data->request,
                                scan_data->policy_token);
      scan_data->any_data->request = NULL;
    }

    g_free(scan_data);
  }
  else
  {
    if (!(network_attrs & ICD_NW_ATTR_AUTOCONNECT))
      return;

    if (status == ICD_POLICY_SCAN_NEW_NETWORK)
      goto new_net;

    for (l = data->found_networks; l; l = l->next)
    {
      struct policy_any_network *network = (struct policy_any_network *)l->data;

      if ((network->network_attrs & ICD_NW_ATTR_LOCALMASK) ==
          (network_attrs & ICD_NW_ATTR_LOCALMASK) &&
          policy_any_string_equal(network->network_type, network_type) &&
          policy_any_string_equal(network->network_id, network_id))
      {
        data->found_networks = g_slist_remove(data->found_networks, network);
        g_free(network->service_type);
        g_free(network->service_id);
        g_free(network->network_type);
        g_free(network->network_id);
        g_free(network);

        if (status != ICD_POLICY_SCAN_EXPIRED_NETWORK)
        {
new_net:
          if ((status == ICD_POLICY_SCAN_NEW_NETWORK ||
               status == ICD_POLICY_SCAN_UPDATE_NETWORK) &&
              scan_data->any_data->min_prio < network_priority)
          {
            struct policy_any_network *network =
                                       g_new0(struct policy_any_network, 1);

            network->service_type = g_strdup(service_type);
            network->service_attrs = service_attrs;
            network->service_id = g_strdup(service_id);
            network->network_type = g_strdup(network_type);
            network->network_attrs = network_attrs;
            network->network_id = g_strdup(network_id);
            network->network_priority = network_priority;
            network->signal = signal;
            data->found_networks = g_slist_insert_sorted(
                data->found_networks, network, policy_any_sort_network);
            scan_data->any_data->iaps_added = TRUE;
          }

          return;
        }
      }
    }

    ILOG_WARN("policy any could not find non-existent network %s/%0x/%s",
                network_type, network_attrs, network_id);
  }
}

/**
 * Clean up internal policy module data structures for a request that has
 * previously reported #ICD_POLICY_WAITING.
 *
 * @param request  the request that is to be removed or NULL for all
 * @param private  private data for the module
 */
static void
policy_any_cancel_request(struct icd_policy_request *request,
                          gpointer *private)
{
  struct policy_any_data *data = (struct policy_any_data *)*private;

  if (!request || data->request == request)
  {
    GSList *l = data->ongoing_scans;

    for (l = data->ongoing_scans; l; l = l->next)
    {
      if (l->data)
      {
        ILOG_DEBUG("any connection stopping scan for %p, %p",
                    policy_any_scan_cb, l->data);
        data->scan_stop(policy_any_scan_cb, l->data);
        g_free(l->data);
      }
    }

    g_slist_free(data->ongoing_scans);
    data->ongoing_scans = NULL;

    for (l = data->found_networks; l; l = l->next)
    {
      struct policy_any_network *network = (struct policy_any_network *)l->data;

      if (network)
      {
        g_free(network->service_type);
        g_free(network->service_id);
        g_free(network->network_type);
        g_free(network->network_id);
        g_free(network);
      }
      else
        ILOG_ERR("any connection network is NULL");
    }

    g_slist_free(data->found_networks);
    data->found_networks = NULL;
    data->request = NULL;
  }
}

/**
 * Policy module destruction function. Will be called before unloading the
 * module.
 *
 * @param private  a reference to the private data
 */
static void
policy_any_destruct(gpointer *private)
{
  policy_any_cancel_request(NULL, private);
  g_free(*private);
  *private = NULL;
}

/**
 * Get the highest priority in the existing requests if change while
 * connected is on
 *
 * @param existing_requests  existing requests
 *
 * @return                   the highest priority found or -1 if change while
 *                           connected not set
 */
static gint
policy_any_get_prio(const GSList *existing_requests)
{
  GConfClient *gconf = gconf_client_get_default();
  gint rv = -1;

  if (gconf_client_get_bool(gconf, CHANGE_WHILE_CONNECTED_KEY, NULL))
  {
    const GSList *l;

    for (l = existing_requests; l; l = l->next)
    {
      struct icd_policy_request *request =
          (struct icd_policy_request *)l->data;

      if (request && rv < request->network_priority)
        rv = request->network_priority;
    }
  }

  g_object_unref(gconf);

  return rv;
}

/**
 * Create a new scan data structure and add it to the list of ongoing scans.
 *
 * @param any_data        module data
 * @param new_request     the new connection request
 * @param policy_done_cb  callback to call when policy has been decided
 * @param policy_token    policy token
 *
 * @return  scan data structure
 */
static struct policy_scan_data*
policy_any_scan_data_new(struct policy_any_data *any_data,
                         struct icd_policy_request *new_request,
                         icd_policy_request_new_cb_fn policy_done_cb,
                         gpointer policy_token)
{
  struct policy_scan_data *scan_data;
  scan_data = g_new0(struct policy_scan_data, 1);
  scan_data->any_data = any_data;
  scan_data->policy_done_cb = policy_done_cb;
  scan_data->policy_token = policy_token;
  any_data->ongoing_scans = g_slist_prepend(any_data->ongoing_scans, scan_data);
  return scan_data;
}

/**
 * Get a list of network types to scan
 * @return  list of network types that caller needs to free
 */
static GSList*
policy_any_get_types(struct policy_any_data *data)
{
  GSList *auto_connect_list;
  GConfClient *gconf = gconf_client_get_default();
  GSList *scan_types_list_old;
  GSList *scan_types_list;
  GSList *iaps_list;
  GError *err = NULL;

  auto_connect_list = gconf_client_get_list(gconf, AUTO_CONNECT_KEY,
                                            GCONF_VALUE_STRING, NULL);

  if (auto_connect_list == NULL)
  {
    g_object_unref(gconf);
    return NULL;
  }

  scan_types_list = auto_connect_list;
  gchar *p = auto_connect_list->data;

  if (p && !strcmp(p, "*"))
  {
    GSList *all_types_list;

    if (auto_connect_list->next)
      ILOG_WARN("any connection wildcard '*' allowed only by itself");

    do
    {
      g_free(scan_types_list->data);
      scan_types_list = g_slist_delete_link(scan_types_list, scan_types_list);
    }
    while (scan_types_list);

    all_types_list = gconf_client_all_dirs(
                           gconf, ICD_GCONF_NETWORK_MAPPING, &err);

    if (err)
    {
      ILOG_WARN("any connection could not find network types for '*': %s",
                err->message);
      g_clear_error(&err);
    }

    if (all_types_list == NULL)
    {
      g_object_unref(gconf);
      return NULL;
    }

    do
    {
      if (all_types_list->data)
      {
        p = g_strrstr((const gchar *)all_types_list->data, "/");

        if (p && p != (gchar *)-1)
        {
          if (!strcmp(p, "*"))
            ILOG_WARN("any connection wildcard '*' not allowed "
                      "as network type name");
          else
          {
            scan_types_list =
                g_slist_prepend(scan_types_list, g_strdup(p + 1));
          }
        }

        g_free(all_types_list->data);
      }

      all_types_list = g_slist_delete_link(all_types_list, all_types_list);
    }
    while (all_types_list);
  }
  else
  {
    GSList *l;

    for (l = auto_connect_list; l; l = l->next)
    {
      gchar *s = l->data;

      if (s && !strcmp(s, "*"))
      {
        ILOG_WARN("any connection wildcard '*' allowed only as first entry");
        g_free(s);
        scan_types_list = g_slist_delete_link(scan_types_list, l);
      }
    }
  }

  if (scan_types_list)
  {
    iaps_list = gconf_client_all_dirs(gconf, ICD_GCONF_PATH, NULL);

    if (iaps_list)
    {
      scan_types_list_old = scan_types_list;
      scan_types_list = NULL;

      do
      {
        gchar *s = g_strconcat((const gchar *)iaps_list->data, "/type", NULL);

        gchar *iap_type = gconf_client_get_string(gconf, s, NULL);
        g_free(s);

        if (iap_type)
        {
          GSList *l;

          for (l = scan_types_list_old; l; l = l->next)
          {
            s = (gchar *)l->data;

            if (!strcmp(s, iap_type))
            {
              scan_types_list = g_slist_prepend(scan_types_list, s);
              scan_types_list_old = g_slist_delete_link(scan_types_list_old, l);
              ILOG_DEBUG("network type '%s' IAP '%s' found in gconf",
                      (gchar *)scan_types_list->data, (gchar *)iaps_list->data);
              break;
            }
          }
        }
        else
        {
          ILOG_ERR("Cannot find %s/type, your gconf is perhaps corrupted. "
                   "Skipping this IAP.", (gchar *)iaps_list->data);
        }

        g_free(iap_type);
        g_free(iaps_list->data);
        iaps_list = g_slist_delete_link(iaps_list, iaps_list);
      }
      while (iaps_list);
    }
    else
    {
      scan_types_list_old = scan_types_list;
      scan_types_list = NULL;
    }

    while (scan_types_list_old)
    {
      if (data->srv_check((const gchar *)scan_types_list_old->data))
      {
        ILOG_DEBUG("service module found for network type '%s'",
                   (gchar *)scan_types_list_old->data);
        scan_types_list = g_slist_prepend(scan_types_list,
                                        scan_types_list_old->data);
      }
      else
      {
        ILOG_DEBUG("network type '%s' IAPs not found in gconf",
                   (gchar *)scan_types_list_old->data);
        g_free(scan_types_list_old->data);
      }

      scan_types_list_old = g_slist_delete_link(scan_types_list_old,
                                              scan_types_list_old);
    }
  }

  g_object_unref(gconf);
  return scan_types_list;
}

/**
 * New connection request policy function.
 *
 * @param new_request        the new connection request
 * @param existing_requests  currently existing requests
 * @param policy_done_cb     callback to call when policy has been decided
 * @param policy_token       token to pass to the callback
 * @param private            the private member of the icd_request_api
 *                           structure
 *
 * @todo  here we should make a new ASK request and merge with that
 */
static void
policy_any_new_request(struct icd_policy_request *new_request,
                       const GSList *existing_requests,
                       icd_policy_request_new_cb_fn policy_done_cb,
                       gpointer policy_token, gpointer *private)
{
  struct policy_any_data *data = (struct policy_any_data *)*private;
  gboolean scan_started;

  if (strcmp(new_request->network_id, OSSO_IAP_ANY))
  {
    policy_done_cb(ICD_POLICY_ACCEPTED, new_request, policy_token);
    return;
  }

  if (new_request->attrs & ICD_POLICY_ATTRIBUTE_HAS_CONNECTIONS)
  {
    ILOG_INFO("any connection request %p already has connections", new_request);
    policy_done_cb(ICD_POLICY_ACCEPTED, new_request, policy_token);
    return;
  }

  if (data->ongoing_scans)
  {
    GSList *l;

    for (l = data->ongoing_scans; l; l = l->next)
    {
      ILOG_ERR("any connection waiting for OSSO_IAP_ANY request %p "
               "scan_data %p reply", data->request->request_token, l->data);
    }

    policy_done_cb(ICD_POLICY_REJECTED, new_request, policy_token);

    return;
  }

  data->request = new_request;
  data->scan_types_list = policy_any_get_types(data);
  data->min_prio = policy_any_get_prio(existing_requests);

  ILOG_DEBUG("any connection request %p scanning for networks with prio > %d",
             data->request->request_token, data->min_prio);

  scan_started = FALSE;

  while(data->scan_types_list)
  {
    gchar *type = (gchar *)data->scan_types_list->data;
    data->scan_types_list = g_slist_delete_link(data->scan_types_list,
                                              data->scan_types_list);

    if (type && *type)
    {
      struct policy_scan_data *scan_data = policy_any_scan_data_new(
                          data, new_request, policy_done_cb, policy_token);
      ILOG_DEBUG("any connection starting scan for '%s'", type);
      data->scan_start(type, ICD_NW_SEARCH_SCOPE_SAVED,
                       policy_any_scan_cb, scan_data);
      scan_started = TRUE;
    }

    g_free(type);
  }

  if (!scan_started)
  {
    new_request->attrs |= ICD_POLICY_ATTRIBUTE_HAS_CONNECTIONS;
    policy_done_cb(ICD_POLICY_ACCEPTED, new_request, policy_token);
  }
}

/**
 * Policy module initialization function.
 *
 * @param policy_api      policy API structure to be filled in by the module
 * @param add_network     function to add a network in response to a policy
 * @param merge_requests  function to merge requests
 * @param make_request    function for creating a new request
 * @param scan_start      function for scanning networks
 * @param scan_stop       function for stopping network scanning
 */
void
icd_policy_init(struct icd_policy_api *policy_api,
                icd_policy_nw_add_fn add_network,
                icd_policy_request_merge_fn merge_requests,
                icd_policy_request_make_new_fn make_request,
                icd_policy_scan_start_fn scan_start,
                icd_policy_scan_stop_fn scan_stop,
                icd_policy_nw_close_fn nw_close,
                icd_policy_network_priority_fn priority,
                icd_policy_service_module_check_fn srv_check)
{
  struct policy_any_data *data = g_new0(struct policy_any_data, 1);

  data->add_network = add_network;
  data->merge = merge_requests;
  data->scan_start = scan_start;
  data->scan_stop = scan_stop;
  data->srv_check = srv_check;

  policy_api->new_request = policy_any_new_request;
  policy_api->cancel_request = policy_any_cancel_request;
  policy_api->destruct = policy_any_destruct;
  policy_api->private = data;
}

/** @} */
