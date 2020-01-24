/**
@file icd_scan.c
@copyright GNU GPLv2 or later

@addtogroup icd_scan Network scan and scan result handling
@ingroup internal

 * @{ */

#include "icd_log.h"
#include "icd_scan.h"
#include "icd_status.h"
#include "icd_network_api.h"
#include "icd_network_priority.h"
#include "icd_srv_provider.h"
#include "icd_gconf.h"

#include <time.h>
#include <string.h>

static gboolean icd_scan_network(struct icd_network_module *module,
                                 const gchar *network_type);

/** scan listener list data */
struct icd_scan_listener {
  /** network type */
  gchar *type;
  /** callback */
  icd_scan_cb_fn cb;
  /** callback user data */
  gpointer user_data;
};

/** helper structure for communicating module and expiration to hash table
 * remove callback */
struct icd_scan_expire_network_data {
  /** module */
  struct icd_network_module *module;
  /** expiration time */
  guint expire;
};

/** scan status name strings */
static const gchar const *icd_scan_status_names[] =
{
  "ICD_SCAN_NEW",
  "ICD_SCAN_UPDATE",
  "ICD_SCAN_NOTIFY",
  "ICD_SCAN_EXPIRE",
  "ICD_SCAN_COMPLETE"
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
inline static gboolean
string_equal(const char *a, const char *b)
{
  if (!a)
    return !b;

  if (b)
    return !strcmp(a, b);

  return FALSE;
}

/**
 * @brief Remove all matching listener callback - user data tuples from a module
 *
 * @param module the network module
 * @param cb the callback to remove or NULL to remove all callbacks
 * @param user_data user data to remove
 *
 */
void
icd_scan_listener_remove(struct icd_network_module *module,
                         icd_scan_cb_fn cb,
                         gpointer user_data)
{
  GSList *l = module->scan_listener_list;

  while (l)
  {
    GSList *next = l->next;
    struct icd_scan_listener *scan_listener =
        (struct icd_scan_listener *)l->data;

    if (!cb ||
        (scan_listener && scan_listener->cb == cb &&
         scan_listener->user_data == user_data))
    {
      g_free(scan_listener->type);
      g_free(scan_listener);
      module->scan_listener_list =
          g_slist_delete_link(module->scan_listener_list, l);
    }

    l = next;
  }
}

/**
 * Unregister all matching callback - user data tuples from receiving scan
 * results
 *
 * @param cb         the same callback as given in icd_scan_results_request()
 * @param user_data  the same user_data as given in
 *                   icd_scan_results_request()
 *
 * @return  TRUE if the callback - user_data tuple existed and was removed;
 *          FALSE otherwise
 */
gboolean
icd_scan_results_unregister(icd_scan_cb_fn cb, gpointer user_data)
{
  GSList *l;
  gboolean rv = FALSE;

  if (!cb)
  {
    ILOG_ERR("cannot remove listener callback that is NULL");
    return rv;
  }

  for (l = icd_context_get()->nw_module_list; l; l = l->next)
  {
    struct icd_network_module *module = (struct icd_network_module *)l->data;

    if (!module)
      ILOG_ERR("network module in list is NULL");
    else
    {
      rv = TRUE;
      icd_scan_listener_remove(module, cb, user_data);
    }
  }

  return rv;
}

/**
 * Set up the scan cache for a network module
 * @param module  network module
 * @return  TRUE on success, FALSE if scan cache already exists
 */
gboolean
icd_scan_cache_init(struct icd_network_module *module)
{
  if (module->scan_cache_table)
  {
    ILOG_ERR("scan cache already exists");
    return FALSE;
  }

  module->scan_cache_table =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  return TRUE;
}

/**
 * Free an icd_scan_cache structure
 * @param cache_entry  cache entry to free
 */
void
icd_scan_cache_entry_free(struct icd_scan_cache *cache_entry)
{
  GSList *l;

  for (l = cache_entry->srv_provider_list; l; l = g_slist_delete_link(l, l))
  {
    struct icd_scan_srv_provider *provider =
        (struct icd_scan_srv_provider *)l->data;

    if (provider)
    {
      g_free(provider->service_type);
      g_free(provider->service_name);
      g_free(provider->service_id);
    }

    g_free(provider);
  }

  g_free(cache_entry->network_type);
  g_free(cache_entry->network_name);
  g_free(cache_entry->network_id);
  g_free(cache_entry->station_id);
  g_free(cache_entry);
}

/**
 * Send the cache entry to the listener if the network type matches
 *
 * @param srv_provider  send only this service provider to the listener if
 *                      set; send all network and service provider entries if
 *                      NULL
 * @param cache_entry   the cache entry
 * @param listener      the listener
 * @param status        the status of the supplied cache entry
 *
 * @return  TRUE if the type matched and listener was updated; FALSE
 *          otherwise
 */
static gboolean
icd_scan_listener_send_entry(struct icd_scan_srv_provider *srv_provider,
                             struct icd_scan_cache *cache_entry,
                             struct icd_scan_listener *listener,
                             enum icd_scan_status status)
{
  if (!cache_entry)
  {
    ILOG_ERR("scan cannot send NULL cache_entry, because nw type unknown");
    return FALSE;
  }

  if (listener->type &&
      strncmp(listener->type, cache_entry->network_type,
              strlen(listener->type)))
  {
    return FALSE;
  }

  if (status == ICD_SCAN_COMPLETE)
    listener->cb(ICD_SCAN_COMPLETE, 0, cache_entry, listener->user_data);
  else if (srv_provider)
  {
    ILOG_DEBUG("sending %s nw %s/%0x/%s srv %s/%0x/%s",
               icd_scan_status_names[status],
               cache_entry->network_type,
               cache_entry->network_attrs,
               cache_entry->network_id,
               srv_provider->service_type,
               srv_provider->service_attrs,
               srv_provider->service_id);
    listener->cb(status, srv_provider, cache_entry, listener->user_data);
  }
  else
  {
    GSList *l;

    if (!(cache_entry->network_attrs & ICD_NW_ATTR_SRV_PROVIDER))
    {
      listener->cb(status, NULL, cache_entry, listener->user_data);
      ILOG_DEBUG("sending %s nw %s/%0x/%s srv -/-/-",
                 icd_scan_status_names[status],
                 cache_entry->network_type,
                 cache_entry->network_attrs,
                 cache_entry->network_id);
    }

    for (l = cache_entry->srv_provider_list; l; l = l->next)
    {
      struct icd_scan_srv_provider *provider =
          (struct icd_scan_srv_provider *)l->data;

      if (provider)
      {
        ILOG_DEBUG("sending %s nw %s/%0x/%s srv %s/%0x/%s",
                   icd_scan_status_names[status],
                   cache_entry->network_type,
                   cache_entry->network_attrs,
                   cache_entry->network_id,
                   provider->service_type,
                   provider->service_attrs,
                   provider->service_id);
        listener->cb(status, provider, cache_entry, listener->user_data);
      }
    }
  }

  return TRUE;
}

/**
 * Check for elements, return immediately on first element found
 *
 * @param key        the network_id, not used
 * @param value      the icd_scan_cache_list
 * @param user_data  not used
 *
 * @return  TRUE on first non-NULL element found
 */
static gboolean
icd_scan_cache_element_check(gpointer key,
                             gpointer value,
                             gpointer user_data)
{
  struct icd_scan_cache_list *scan_cache_list =
      (struct icd_scan_cache_list *)value;
  GSList *cache_list;
  gboolean rv = FALSE;

  if (scan_cache_list && (cache_list = scan_cache_list->cache_list))
  {
    if (cache_list->data)
      rv = TRUE;
  }

  ILOG_DEBUG("scan cache contains %d elements", rv ? 1 : 0);

  return rv;
}

/**
 * Check whether a scan cache has any elements
 * @param module  network module
 * @return  TRUE if there are elements, FALSE otherwise
 */
static gboolean
icd_scan_cache_has_elements(struct icd_network_module *module)
{
  int user_data = 0;

  return !!g_hash_table_find(module->scan_cache_table,
                             (GHRFunc)icd_scan_cache_element_check, &user_data);
}

/**
 * Check if there are any listeners that want scan results
 * @param module  the network module
 * @return  TRUE if there are listeners, FALSE otherwise
 */
static gboolean
icd_scan_listener_exist(struct icd_network_module *module)
{
  return module->scan_listener_list != NULL;
}

/**
 * Notify each matching listener about the change in the cache entry
 *
 * @param module        the network module
 * @param srv_provider  NULL or the specific service provider entry that got
 *                      updated; if non-NULL only this service provider
 *                      associated with the cache_entry will be sent to the
 *                      listeners
 * @param cache_entry   corresponding cache entry that got updated
 * @param status        status of the notification
 */
void
icd_scan_listener_notify(struct icd_network_module *module,
                         struct icd_scan_srv_provider *srv_provider,
                         struct icd_scan_cache *cache_entry,
                         enum icd_scan_status status)
{
  GSList *l;

  for (l = module->scan_listener_list; l; l = l->next)
  {
    icd_scan_listener_send_entry(srv_provider, cache_entry,
                                 (struct icd_scan_listener *)l->data, status);
  }
}

/**
 * Hash table callback for removing an entry. Note that this function is also
 * called outside of hash, so care should be taken when dealing with the hash
 * (that is why list_entry is not deleted inside this function).
 *
 * @param key        the network_id
 * @param value      the icd_scan_cache_list struct
 * @param user_data  expiration time
 *
 * @return  TRUE when all networks for the network_id have been expired and
 *          the hash table element can be removed; FALSE otherwise
 */
static gboolean
icd_scan_expire_network(gpointer key, gpointer value, gpointer user_data)
{
  struct icd_scan_cache_list *scan_cache_list =
      (struct icd_scan_cache_list *)value;
  struct icd_scan_expire_network_data *expire_network_data =
      (struct icd_scan_expire_network_data *)user_data;
  gchar *network_id = (gchar *)key;
  GSList *l = scan_cache_list->cache_list;
  int expired = 0;
  int entries = 0;

  while (l)
  {
    struct icd_scan_cache *cache_entry = (struct icd_scan_cache *)l->data;
    GSList *next = l->next;

    if (cache_entry)
    {
      if (cache_entry->last_seen <= expire_network_data->expire)
      {
        icd_scan_listener_notify(
              expire_network_data->module, NULL, cache_entry, ICD_SCAN_EXPIRE);
        icd_scan_cache_entry_free(cache_entry);
        scan_cache_list->cache_list =
            g_slist_delete_link(scan_cache_list->cache_list, l);
        expired++;
      }
    }
    else
    {
      ILOG_ERR("NULL cache entry in network '%s'", network_id);
      scan_cache_list->cache_list =
          g_slist_delete_link(scan_cache_list->cache_list, l);
    }

    l = next;
    entries++;
  }

  if (scan_cache_list->cache_list)
  {
    if (expired)
    {
      ILOG_DEBUG("network id '%s' expired %d/%d entries", network_id, expired,
                 entries);
    }
    return FALSE;
  }

  ILOG_DEBUG("network id '%s' all entries expired", network_id);
  return TRUE;
}

/**
 * Hash table callback for removing an entry, this version is only called
 * from hash remove func.
 *
 * @param key        the network_id
 * @param value      the icd_scan_cache_list struct
 * @param user_data  expiration time
 *
 * @return  TRUE when all networks for the network_id have been expired and
 *          the hash table element can be removed; FALSE otherwise
 */
static gboolean
icd_scan_expire_network_for_hash(gpointer key, gpointer value,
                                 gpointer user_data)
{
  if (icd_scan_expire_network(key, value, user_data))
  {
    g_free(value);
    return TRUE;
  }

  return FALSE;
}

/**
 * Remove and g_free() the timeout data
 * @param timeout_data  the timeout data to free
 */
static void
icd_scan_timeout_free(struct icd_scan_cache_timeout *timeout_data)
{
  timeout_data->module->scan_timeout_list =
      g_slist_remove(timeout_data->module->scan_timeout_list,
                     timeout_data);

  g_free(timeout_data);
}

/**
 * Cache expiry function
 * @param data  scan cache timeout data
 * @return      FALSE to remove the timeout
 */
static gboolean
icd_scan_cache_expire(gpointer data)
{
  struct icd_scan_cache_timeout *timeout_data;

  timeout_data = (struct icd_scan_cache_timeout *)data;

  if (timeout_data->module->scan_progress)
  {
    ILOG_DEBUG("deferred scan cache expiration for '%s' due to new scan",
               timeout_data->module->name);
  }
  else
  {
    struct icd_scan_expire_network_data user_data;

    user_data.module = timeout_data->module;
    user_data.expire = time(0) - timeout_data->module->nw.search_lifetime;

    g_hash_table_foreach_remove(timeout_data->module->scan_cache_table,
                                (GHRFunc)icd_scan_expire_network_for_hash,
                                &user_data);
  }

  icd_scan_timeout_free(timeout_data);

  return FALSE;
}

/**
 * Get the scan cache list associated with the network id
 *
 * @param module      network module
 * @param network_id  network id
 *
 * @return  the scan cache list or NULL if not found
 */
struct icd_scan_cache_list *
icd_scan_cache_list_lookup(struct icd_network_module *module,
                           const gchar *network_id)
{
  if (!module || !module->scan_cache_table)
    return NULL;

  return (struct icd_scan_cache_list *)
      g_hash_table_lookup(module->scan_cache_table, network_id);
}

/**
 * Function that removes all the IAPs for a given module from cache that
 * match name.
 *
 * @param module  the network module
 * @param IAP     name
 *
 * @return        TRUE so that foreach func will continue
 */
static gboolean
icd_scan_cache_remove_iap_for_module(struct icd_network_module *module,
                                     gpointer user_data)
{
  struct icd_scan_cache_list *scan_cache_list;
  GSList *net_type;

  scan_cache_list = icd_scan_cache_list_lookup(module,
                                               (const gchar *)user_data);

  if (!scan_cache_list)
    return TRUE;

  for (net_type = module->network_types; net_type; net_type = net_type->next)
  {
    GSList *cache_list = scan_cache_list->cache_list;
    const gchar *network_type = (const gchar *)net_type->data;
    GSList *next;

    while (cache_list)
    {
      struct icd_scan_cache *cache = (struct icd_scan_cache *)cache_list->data;
      next = cache_list->next;

      if (cache && cache->network_attrs & ICD_NW_ATTR_IAPNAME &&
          string_equal(cache->network_type, network_type))
      {
        ILOG_DEBUG("removing %s, name=%s, attrs=0x%x, type=%s",
                   cache->network_id,
                   cache->network_name,
                   cache->network_attrs,
                   cache->network_type);

        icd_scan_listener_notify(module, NULL, cache, ICD_SCAN_EXPIRE);
        icd_scan_cache_entry_free(cache);
        scan_cache_list->cache_list =
            g_slist_delete_link(scan_cache_list->cache_list, cache_list);

      }

      cache_list = next;
    }
  }

  return TRUE;
}

/**
 * Function that removes all the IAPs from cache that match name.
 * @param iap_name  IAP name
 */
void
icd_scan_cache_remove_iap(gchar *iap_name)
{
  icd_network_api_foreach_module(icd_context_get(),
                                 icd_scan_cache_remove_iap_for_module,
                                 iap_name);
}

/**
 * Remove scan cache from scan list, the removed entry does not call
 * listeners.
 *
 * @param cache_list     the icd_scan_cache_list struct
 * @param network_id     network identifier
 * @param network_type   the network type
 * @param network_attrs  network attributes
 *
 * @return  TRUE if scan entry was removed; FALSE otherwise
 */
gboolean
icd_scan_cache_entry_remove(struct icd_scan_cache_list *scan_cache_list,
                            const gchar *network_id,
                            const gchar *network_type,
                            const guint network_attrs)
{
  GSList *l = scan_cache_list->cache_list;
  gint entries = 0;

  while (l)
  {
    struct icd_scan_cache *cache = (struct icd_scan_cache *)l->data;
    GSList *next = l->next;

    if (cache && cache->network_attrs == network_attrs &&
        string_equal(cache->network_type, network_type))
    {
      GSList *tmp;

      icd_scan_cache_entry_free(cache);
      tmp = g_slist_delete_link(scan_cache_list->cache_list, l);
      entries++;
      l = next;
      scan_cache_list->cache_list = tmp;
    }

    l = next;
  }

  if (!scan_cache_list->cache_list)
    ILOG_DEBUG("network id '%s' all entries (%d) removed", network_id, entries);

  return !!entries;
}

/**
 * Tear down the scan cache and its hash table
 * @param module  the netowork module
 */
void
icd_scan_cache_remove(struct icd_network_module *module)
{
  GSList *l;

  if (module->scan_timeout_rescan)
  {
    g_source_remove(module->scan_timeout_rescan);
    module->scan_timeout_rescan = 0;
  }

  l = module->scan_timeout_list;

  while (l)
  {
    struct icd_scan_cache_timeout *timeout =
        (struct icd_scan_cache_timeout *)l->data;
    GSList *next = l->next;

    if (timeout)
    {
      g_source_remove(timeout->id);
      g_free(timeout);
    }
    else
      ILOG_ERR("timeout data NULL");

    module->scan_timeout_list =
        g_slist_delete_link(module->scan_timeout_list, l);
    l = next;
  }

  if (module->scan_cache_table)
  {
    struct icd_scan_expire_network_data user_data;

    user_data.module = module;
    user_data.expire = time(0) + 1;
    g_hash_table_foreach_remove(module->scan_cache_table,
                                (GHRFunc)icd_scan_expire_network_for_hash,
                                &user_data);
  }

  icd_scan_listener_remove(module, NULL, NULL);
}

/**
 * Send all cache entries to the listener
 *
 * @param key        the network_id
 * @param value      the icd_scan_cache_list
 * @param user_data  the icd_scan_listener
 */
static void
icd_scan_listener_send_list(gpointer key, gpointer value, gpointer user_data)
{
  GSList *l;

  for (l = ((struct icd_scan_cache_list *)value)->cache_list; l; l = l->next)
  {
    icd_scan_listener_send_entry(NULL, (struct icd_scan_cache *)l->data,
        (struct icd_scan_listener *)user_data, ICD_SCAN_NEW);
  }
}

/**
 * Rescan timeout function
 * @param data  scan cache timeout data
 * @return      FALSE so that the timeout won't repeat
 */
static gboolean
icd_scan_cache_rescan(gpointer data)
{
  struct icd_network_module *module = (struct icd_network_module *)data;

  module->scan_timeout_rescan = 0;

  if (icd_scan_listener_exist(module))
    icd_scan_network(module, NULL);
  else
    ILOG_INFO("module '%s' has no listeners, scan not restarted", module->name);

  return FALSE;
}

/**
 * Add a rescan timeout.
 *
 * @param module   the network module
 * @param seconds  seconds until timeout
 *
 * @return         id the new timeout id
 */
static guint
icd_scan_timeout_rescan_add(struct icd_network_module *module, guint seconds)
{
  if (module->scan_timeout_rescan)
    g_source_remove(module->scan_timeout_rescan);

  module->scan_timeout_rescan =
      g_timeout_add(1000 * seconds, icd_scan_cache_rescan, module);

  return module->scan_timeout_rescan;
}

/**
 * Send scan stop status
 * @param module  network module
 */
static void
icd_scan_status_stop(struct icd_network_module *module)
{
  GSList *l;

  if (module->nw.start_search)
  {
    for (l = module->network_types; l; l = l->next)
    {
      if (l->data)
      {
        ILOG_INFO("scan stop for type '%s'", (const gchar *)l->data);
        icd_status_scan_stop((const gchar *)l->data);
      }
    }
  }
}

/**
 * Add a pending timeout function. The timeout function must remove the
 * timeout data structure if the same callback is not called repeatedly.
 *
 * @param module           the network module
 * @param expire_function  function to call
 * @param seconds          seconds until timeout
 *
 * @return  id the timeout id
 */
static guint
icd_scan_timeout_add(struct icd_network_module *module,
                     GSourceFunc expire_function,
                     guint seconds)
{
  struct icd_scan_cache_timeout *scan_cache_timeout;

  scan_cache_timeout = g_new0(struct icd_scan_cache_timeout, 1);
  scan_cache_timeout->module = module;

  scan_cache_timeout->id =
      g_timeout_add(1000 * seconds, expire_function, scan_cache_timeout);

  module->scan_timeout_list =
      g_slist_prepend(module->scan_timeout_list, scan_cache_timeout);

  return scan_cache_timeout->id;
}

/**
 * Callback function for scan results
 *
 * @param status           status of the operation
 * @param network_name     name of the network displayable to user
 * @param network_type     type of network
 * @param network_attrs    network attributes
 * @param network_id       network id
 * @param signal           signal level
 * @param station_id       base station id
 * @param dB               raw signal strength
 * @param search_cb_token  the network module structure
 */
static void
icd_scan_cb(enum icd_network_search_status status, gchar *network_name,
            gchar *network_type, const guint network_attrs, gchar *network_id,
            enum icd_nw_levels signal, gchar *station_id, gint dB,
            gpointer search_cb_token)
{
  struct icd_network_module *module =
      (struct icd_network_module *)search_cb_token;
  struct icd_scan_cache_list *scan_cache_list;
  enum icd_scan_status scan_status = ICD_SCAN_NEW;
  struct icd_scan_cache *cache_entry = NULL;
  GSList *l;
  guint now;

  if (!module)
  {
    ILOG_ERR("ICd search_cb_token returned from module is NULL");
    return;
  }

  if (!module->scan_progress && status != ICD_NW_SEARCH_EXPIRE)
  {
    ILOG_INFO("scan results returned from '%s', but no scan ongoing",
              module->name);
    return;
  }

  now = time(0);

  if (network_name && network_type && network_id)
  {
    scan_cache_list = icd_scan_cache_list_lookup(module, network_id);

    if (scan_cache_list)
    {
      cache_entry = icd_scan_cache_entry_find(scan_cache_list, network_type,
                                              network_attrs);

      if (status == ICD_NW_SEARCH_EXPIRE)
      {
        struct icd_scan_expire_network_data user_data;

        user_data.module = module;
        user_data.expire = now;
        icd_scan_expire_network(network_id, scan_cache_list, &user_data);
        return;
      }

      if (cache_entry)
      {
        cache_entry->last_seen = now;

        if (cache_entry->signal >= signal)
        {
          struct icd_scan_cache new_cache_entry;

          memcpy(&new_cache_entry, cache_entry, sizeof(new_cache_entry));
          new_cache_entry.station_id = station_id;
          new_cache_entry.dB = dB;
          icd_scan_listener_notify(module, NULL, &new_cache_entry,
                                   ICD_SCAN_NOTIFY);
          scan_status = ICD_SCAN_NOTIFY;
        }
        else
        {
          cache_entry->signal = signal;
          cache_entry->dB = dB;
          g_free(cache_entry->station_id);
          cache_entry->station_id = g_strdup(station_id);
          icd_scan_listener_notify(module, NULL, cache_entry,
                                   ICD_SCAN_UPDATE);
          scan_status = ICD_SCAN_UPDATE;
        }
      }
    }
    else if (status == ICD_NW_SEARCH_EXPIRE)
      return;

    if (!cache_entry)
    {
      cache_entry = g_new0(struct icd_scan_cache, 1);
      cache_entry->network_name = g_strdup(network_name);
      cache_entry->network_type = g_strdup(network_type);
      cache_entry->network_attrs = network_attrs;
      cache_entry->network_id = g_strdup(network_id);
      cache_entry->signal = signal;
      cache_entry->station_id = g_strdup(station_id);
      cache_entry->dB = dB;
      cache_entry->last_seen = now;
      cache_entry->network_priority =
          icd_network_priority_get(NULL, NULL, cache_entry->network_type,
                                   cache_entry->network_attrs);

      if (!scan_cache_list)
        scan_cache_list = g_new0(struct icd_scan_cache_list, 1);

      icd_scan_cache_entry_add(module, scan_cache_list, cache_entry);
      icd_scan_listener_notify(module, NULL, cache_entry, ICD_SCAN_NEW);
      scan_status = ICD_SCAN_NEW;
    }

    icd_srv_provider_identify(module, cache_entry, scan_status);

    if (status != ICD_NW_SEARCH_COMPLETE)
      return;
  }

  if (status == ICD_NW_SEARCH_COMPLETE)
  {
    struct icd_scan_expire_network_data user_data;

    user_data.module = module;
    user_data.expire = time(NULL) - module->nw.search_lifetime;
    g_hash_table_foreach_remove(module->scan_cache_table,
                                (GHRFunc)icd_scan_expire_network_for_hash,
                                &user_data);

    cache_entry = g_new0(struct icd_scan_cache, 1);

    for (l = module->network_types; l; l = l->next)
    {
      cache_entry->network_type = (gchar *)l->data;
      icd_scan_listener_notify(module, NULL, cache_entry,
                               ICD_SCAN_COMPLETE);
    }

    g_free(cache_entry);
    module->scan_progress = FALSE;
    ILOG_INFO("module '%s' scan completed", module->name);

    icd_scan_status_stop(module);

    icd_scan_timeout_rescan_add(module, module->nw.search_interval);

    icd_scan_timeout_add(module, icd_scan_cache_expire,
                         module->nw.search_lifetime);

    return;
  }

  ILOG_ERR("returned scan result (%s/%s/%s) has NULL components", network_name,
           network_type, network_id);
}

/**
 * Send scan start status
 * @param module  network module
 */
static void
icd_scan_status_start(struct icd_network_module *module)
{
  GSList *l;

  for (l = module->network_types; l; l = l->next)
  {
    if (l->data)
    {
      ILOG_INFO("scan start for type '%s'", (const gchar *)l->data);
      icd_status_scan_start((const gchar *)l->data);
    }
  }
}

/**
 * Request one network module to initiate scan with given scope
 *
 * @param module        the network module
 * @param network_type  network type to scan for
 *
 * @return  TRUE if scan was started or already ongoing, FALSE otherwise
 */
static gboolean
icd_scan_network(struct icd_network_module *module, const gchar *network_type)
{
  if (module->scan_progress)
    return TRUE;

  if (!module->nw.start_search)
  {
    ILOG_WARN("module '%s' does not have a scan function", module->name);
    return FALSE;
  }

  icd_scan_status_start(module);

  module->scan_progress = TRUE;
  module->nw.start_search(NULL, module->scope, icd_scan_cb, module,
                          &module->nw.private);

  return TRUE;
}

/**
 * Notify a (new) listener of the current cache
 * @param module    the network module
 * @param listener  the listener to update with the current cache
 */
static void
icd_scan_listener_send_cache(struct icd_network_module *module,
                             struct icd_scan_listener *listener)
{
  if (icd_scan_cache_has_elements(module))
  {
    g_hash_table_foreach(
        module->scan_cache_table, icd_scan_listener_send_list, listener);

    if (module->scan_timeout_rescan)
    {
      struct icd_scan_cache cache_entry;

      memset (&cache_entry, 0, sizeof(cache_entry));
      cache_entry.network_type = listener->type;

      icd_scan_listener_send_entry(
               NULL, &cache_entry, listener, ICD_SCAN_COMPLETE);
    }
  }
}

/**
 * Add a scan result listener to a module
 *
 * @param module     the module whose scan results to report
 * @param type       the network type to receive scan results for
 * @param cb         listener callback
 * @param user_data  user data to pass to the callback
 *
 * @return  TRUE when the callback is added to the list, FALSE on error
 */
static gboolean
icd_scan_listener_add(struct icd_network_module *module,
                      const gchar *type,
                      icd_scan_cb_fn cb,
                      gpointer user_data)
{
  struct icd_scan_listener *listener =
      g_new0(struct icd_scan_listener, 1);

  listener->cb = cb;
  listener->user_data = user_data;
  listener->type = g_strdup(type);
  module->scan_listener_list =
      g_slist_prepend(module->scan_listener_list, listener);

  icd_scan_listener_send_cache(module, listener);

  return TRUE;
}

/**
 * Function requesting scan results; can be called many times to add more
 * than one network type per callback
 *
 * @param type       network type to scan or NULL for all networks
 * @param scope      scope of the scan, see network_api.h
 * @param cb         callback function for the results
 * @param user_data  user data to pass to the callback
 *
 * @return  TRUE if the callback is going to be called; FALSE on error
 */
gboolean
icd_scan_results_request(const gchar *type, const guint scope,
                         icd_scan_cb_fn cb, gpointer user_data)
{
  GSList *l, *m;
  gboolean rv = FALSE;

  if (!cb)
  {
    ILOG_WARN("listener callback cannot be NULL");
    return FALSE;
  }

  for (l = icd_context_get()->nw_module_list; l; l = l->next)
  {
    struct icd_network_module *module =
        (struct icd_network_module *)l->data;

    if (!module)
    {
      ILOG_ERR("network module in list is NULL");
      continue;
    }
    if (module->nw.start_search &&
        (!type || icd_network_api_has_type(module, type)))
    {
      gboolean scan_listener_exist = icd_scan_listener_exist(module);

      for (m = module->scan_listener_list; m; m = m->next)
      {
        struct icd_scan_listener *listener =
            (struct icd_scan_listener *)m->data;

        if (!listener)
          continue;

        if ((!type && !listener->type) ||
            (type && listener->type &&
             !strncmp(listener->type, type, strlen(type))))
        {
          if (cb == listener->cb && user_data == listener->user_data)
            break;
        }
      }

      if (!m)
        icd_scan_listener_add(module, type, cb, user_data);

      if ((!scan_listener_exist && !module->scan_timeout_rescan) ||
          module->scope > scope || !icd_scan_cache_has_elements(module) ||
          icd_gconf_get_iap_bool(NULL, ICD_GCONF_AGGRESSIVE_SCANNING, FALSE))
      {
        module->scope = scope;
        icd_scan_network(module, NULL);
      }

      rv = TRUE;
    }
  }

  if (!rv)
    ILOG_WARN("no module supporting scanning for type '%s' found", type);

  return rv;
}

/**
 * Add an cache entry to the network module
 *
 * @param module       network module
 * @param scan_cache   scan cache list or NULL
 * @param cache_entry  the entry to add
 */
void
icd_scan_cache_entry_add(struct icd_network_module *module,
                         struct icd_scan_cache_list *scan_cache,
                         struct icd_scan_cache *cache_entry)
{
  if (module && scan_cache && cache_entry)
  {
    scan_cache->cache_list = g_slist_prepend(scan_cache->cache_list,
                                             cache_entry);
    g_hash_table_replace(module->scan_cache_table,
                         g_strdup(cache_entry->network_id), scan_cache);
  }
  else
    ILOG_ERR("module, cache list or cache not given when adding scan");
}

/**
 * Find a network entry in the scan cache list
 *
 * @param scan_cache_list  scan cache list
 * @param network_type     network type
 * @param network_attrs    network attrs
 *
 * @return  the scan cache entry or NULL if not found
 */
struct icd_scan_cache *
icd_scan_cache_entry_find(struct icd_scan_cache_list *scan_cache_list,
                          const gchar *network_type, const guint network_attrs)
{
  GSList *l;

  for (l = scan_cache_list->cache_list; l; l = l->next)
  {
    struct icd_scan_cache *cache_entry = (struct icd_scan_cache *)l->data;

    if (cache_entry && cache_entry->network_attrs == network_attrs &&
        string_equal(cache_entry->network_type, network_type))
    {
      return cache_entry;
    }
  }

  return NULL;
}

/** @} */
