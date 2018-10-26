#include <string.h>
#include <time.h>
#include <gconf/gconf-client.h>
#include <glib.h>
#include <osso-ic-gconf.h>
#include <osso-ic-dbus.h>
#include "icd_plugin.h"
#include "srv_provider_api.h"
#include "icd_srv_provider.h"
#include "icd_log.h"
#include "icd_status.h"
#include "icd_network_priority.h"
#include "icd_version.h"

/** service provider entry for the module name */
#define ICD_SRV_PROVIDER_MODULE_NAME   "/module"

/** service provider entry for supported network types */
#define ICD_SRV_PROVIDER_NETWORK_TYPES   "/network_type"

/** pid and exit value structure */
struct pid_notify {
  /** process id */
  pid_t pid;
  /** exit value */
  gint exit_value;
};

/** structure for passing data to module initialization callback */
struct icd_srv_provider_cb_data {
  /** icd context */
  struct icd_context *icd_ctx;

  /** supported service type */
  gchar *service_type;

  /** supported network types */
  GSList *network_types;
};

/** Service identification information */
struct icd_srv_identify {
  /** The network module */
  struct icd_network_module *module;

  /** the signal level */
  enum icd_nw_levels signal;
};

/**
 * @brief  Helper function for comparing two strings where a NULL string is
 *         equal to another NULL string
 *
 * @param  a  string A
 * @param  b  string B
 *
 * @return TRUE if equal, FALSE if unequal
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
 * @brief  Find the srv provider module that is watching a child process exit
 *
 * @param  module     the srv provider module to examine
 * @param  user_data  the pid notify structure
 *
 * @return TRUE to continue searching, FALSE to exit iteration and return a
 *         pointer to the module
 */
static gboolean
icd_srv_provider_foreach_module_pid(struct icd_srv_module *srv_module,
                                    gpointer user_data)
{
  GSList *l;
  struct pid_notify *notify = (struct pid_notify *)user_data;

  for (l = srv_module->pid_list; l; l = l->next)
  {
    pid_t *pid = (pid_t *)l->data;

    if (pid && *pid == notify->pid)
    {
      g_free(pid);
      srv_module->pid_list = g_slist_delete_link(srv_module->pid_list, l);

      if (srv_module->srv.child_exit)
      {
        ILOG_INFO("srv module '%s' notified for pid %d", srv_module->name,
                  notify->pid);
        srv_module->srv.child_exit(notify->pid, notify->exit_value,
                                   &srv_module->srv.private);
      }
      else
      {
        ILOG_WARN("module '%s' cannot be notified about pid %d as child_exit is NULL",
                  srv_module->name, notify->pid);
      }

      return FALSE;
    }
  }

  return TRUE;
}

/**
 * @brief  Iterate over all service provider modules
 *
 * @param  icd_ctx     icd context
 * @param  foreach_fn  the function to call for each module
 * @param  user_data   user data to pass to the function
 *
 * @return a pointer to the module if the iteration function returns FALSE;
 *         NULL otherwise
 */
struct icd_srv_module *
icd_srv_provider_foreach_module(struct icd_context *icd_ctx,
                                icd_srv_provider_foreach_module_fn foreach_fn,
                                gpointer user_data)
{
  GSList *l;

  if (!icd_ctx || !foreach_fn)
  {
    ILOG_ERR("icd_ctx or foreach_fn cannot be NULL");
    return NULL;
  }

  for (l = icd_ctx->srv_module_list; l; l = l->next)
  {
    struct icd_srv_module *module = (struct icd_srv_module *)l->data;

    if (module)
    {
      if (!foreach_fn(module, user_data))
        return module;
    }
    else
      ILOG_WARN("srv module list has NULL module data");
  }

  return NULL;
}

/**
 * @brief  Notify a srv provider module that its child process has exited
 *
 * @param  icd_ctx     the context
 * @param  pid         the process id
 * @param  exit_value  exit value
 *
 * @return TRUE if the process id was in use by the srv provider api; FALSE
 *         if not
 */
gboolean
icd_srv_provider_notify_pid(struct icd_context *icd_ctx, const pid_t pid,
                            const gint exit_value)
{
  struct pid_notify notify;

  notify.pid = pid;
  notify.exit_value = exit_value;

  return icd_srv_provider_foreach_module(icd_ctx,
                                         icd_srv_provider_foreach_module_pid,
                                         &notify) != NULL;
}

/**
 * @brief  Disconnect callback for the service provider module
 *
 * @param  status               status of the disconnect, ignored for now
 * @param  disconnect_cb_token  token passed to the disconnect function
 */
static void
icd_srv_provider_disconnect_cb(enum icd_srv_status status,
                               gpointer disconnect_cb_token)
{
  struct icd_iap *iap = (struct icd_iap *)disconnect_cb_token;


  if (iap)
  {
    icd_srv_provider_disconnect_cb_fn fn =
        (icd_srv_provider_disconnect_cb_fn)iap->srv_disconnect_cb;

    if (fn)
      fn(status, iap->srv_disconnect_cb_user_data);

    iap->srv_disconnect_cb_user_data = NULL;
    iap->srv_disconnect_cb = NULL;
  }
  else
    ILOG_ERR("srv provider disconnect cb returned NULL iap");
}

/**
 * @brief  Disconnect the service module
 *
 * @param  iap        the IAP
 * @param  cb         callback function
 * @param  user_data  user data for the callback function
 *
 * @return TRUE if the service module got called, FALSE if not
 */
gboolean
icd_srv_provider_disconnect(struct icd_iap *iap,
                            icd_srv_provider_disconnect_cb_fn cb,
                            gpointer user_data)
{
  struct icd_context *icd_ctx;
  struct icd_srv_module *module;

  if (!cb)
  {
    ILOG_ERR("srv provider disconnect cb cannot be NULL");
    return FALSE;
  }

  icd_ctx = icd_context_get();

  if (!iap->connection.service_type || !iap->connection.service_id)
  {
    ILOG_DEBUG("no service module for iap %p", iap);
    return FALSE;
  }

  if (iap->srv_disconnect_cb)
  {
    ILOG_INFO("srv provider disconnect already in progress for iap %p", iap);
    return FALSE;
  }

  module = (struct icd_srv_module *)g_hash_table_lookup(
        icd_ctx->srv_type_to_srv_module, iap->connection.service_type);

  if (!module)
  {
    ILOG_ERR("srv type '%s' unknown", iap->connection.service_type);
    return FALSE;
  }

  if (!module->srv.disconnect)
    return FALSE;

  iap->srv_disconnect_cb = cb;
  iap->srv_disconnect_cb_user_data = user_data;

  module->srv.disconnect(iap->connection.service_type,
                         iap->connection.service_attrs,
                         iap->connection.service_id,
                         iap->connection.network_type,
                         iap->connection.network_attrs,
                         iap->connection.network_id,
                         iap->interface_name,
                         icd_srv_provider_disconnect_cb,
                         iap, &module->srv.private);

  return TRUE;
}

/**
 * @brief  Check if there is a service module for a given type. If type is
 *         not given then return TRUE if there is any service module found.
 *
 * @param  network_type  network_type to search, can be NULL
 *
 * @return TRUE if the service module was found; FALSE if not
 */
gboolean
icd_srv_provider_check(const gchar *network_type)
{
  struct icd_context *icd_ctx = icd_context_get();

  if (!g_slist_nth_data(icd_ctx->srv_module_list, 0))
    return FALSE;

  if (network_type)
    return !!g_hash_table_lookup(icd_ctx->nw_type_to_srv_module, network_type);

  return TRUE;
}

/**
 * @brief  Function for notifying ICd that a child process has been started.
 *         The srv provider destruction function will not be called before
 *         all child processes have exited.
 *
 * @param  pid             process id
 * @param  watch_cb_token  the watch callback token given on initialization
 */
static void
icd_srv_provider_watch_pid(const pid_t pid, gpointer watch_cb_token)
{
  struct icd_srv_module *module = (struct icd_srv_module *)watch_cb_token;
  pid_t *ppid;

  if (watch_cb_token)
  {
    ppid = g_new0(pid_t, 1);
    *ppid = pid;
    module->pid_list = g_slist_prepend(module->pid_list, ppid);
    ILOG_DEBUG("added pid %d to module '%s'", *ppid, module->name);
  }
  else
    ILOG_ERR("module NULL while submitting srv module child pid");
}

/**
 * @brief  Function to request closing down a connection due to internal or
 *         external events.
 *
 * @param  status         reason for closing; #ICD_SRV_RESTART if the
 *                        connection needs to be restarted, success or error
 *                        will both close the network connection
 * @param  err_str        NULL if the service provisioning was disconnected
 *                        normally or any ICD_DBUS_ERROR_* from
 *                        osso-ic-dbus.h on error
 * @param  service_type   the service type; currently ignored
 * @param  service_attrs  attributes; currently ignored
 * @param  service_id     internal service id; currently ignored
 * @param  network_type   the network type
 * @param  network_attrs  network attributes
 * @param  network_id     network id
 */
static void
icd_srv_provider_close(enum icd_srv_status status, const gchar *err_str,
                       const gchar *service_type, const guint service_attrs,
                       const gchar *service_id, const gchar *network_type,
                       const guint network_attrs, const gchar *network_id)
{
  struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);

  if (iap)
  {
    ILOG_INFO("srv provider disconnecting IAP %p, srv %s/%0x/%s, nw %s/%0x/%s",
              iap, service_type, service_attrs, service_id, network_type,
              network_attrs, network_id);

    icd_status_disconnect(iap, 0, err_str);

    if (status == ICD_SRV_RESTART)
      icd_iap_restart(iap, ICD_NW_LAYER_ALL);
    else
    {
      if (status == ICD_SRV_ERROR && !err_str)
        err_str = ICD_DBUS_ERROR_NETWORK_ERROR;

      icd_iap_disconnect(iap, err_str);
    }
  }
  else
  {
    ILOG_WARN("disconnect requested for srv %s/%0x/%s, nw %s/%0x/%s but no matching nw",
              service_type, service_attrs,  service_id, network_type,
              network_attrs, network_id);
  }
}

/**
 * @brief  Limited connectivity enabling and disabling.
 *
 * @param  service_type   the service type
 * @param  service_attrs  attributes
 * @param  service_id     internal service id
 * @param  network_type   the network type
 * @param  network_attrs  network attributes
 * @param  network_id     network id
 */
static void
icd_srv_provider_limited_conn(
    const enum icd_srv_limited_conn_status conn_status,
    const gchar *service_type, const guint service_attrs,
    const gchar *service_id, const gchar *network_type,
    const guint network_attrs, const gchar *network_id)
{
  struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);

  if (iap)
  {
    if ( iap->state == ICD_IAP_STATE_SRV_UP )
    {
      gboolean limited_conn = conn_status == ICD_SRV_LIMITED_CONN_ENABLED;

      if (iap->limited_conn != limited_conn)
      {
        iap->limited_conn = limited_conn;
        icd_status_limited_conn(iap, NULL, NULL);
      }
    }
    else
    {
      ILOG_WARN("limited connectivity requested for srv %s/%0x/%s, nw %s/%0x/%s but iap in wrong state (%d)",
                service_type, service_attrs, service_id, network_type,
                network_attrs, network_id, iap->state);
    }
  }
  else
  {
    ILOG_WARN("limited connectivity requested for srv %s/%0x/%s, nw %s/%0x/%s but no matching nw",
              service_type, service_attrs, service_id, network_type,
              network_attrs, network_id);
  }
}

/**
 * @brief  Service provider plugin initialization callback function
 *
 * @param  module_name    the moduel filename
 * @param  handle         module handle to pass unloading function
 * @param  init_function  a pointer to the plugin init function
 * @param  cb_data        user data passed to icd_plugin_load_all
 *
 * @return TRUE on success, FALSE on failure whereby the module is unloaded
 */
static gboolean
icd_srv_provider_init(const gchar *module_name, void *handle,
                      gpointer init_function, gpointer cb_data)
{
  struct icd_srv_provider_cb_data *data =
      (struct icd_srv_provider_cb_data *)cb_data;
  struct icd_srv_module *module = (struct icd_srv_module *)g_hash_table_lookup(
        data->icd_ctx->srv_type_to_srv_module, data->service_type);
  icd_srv_init_fn init = (icd_srv_init_fn)init_function;
  gboolean rv = FALSE;

  if (!module)
  {
    module = g_new0(struct icd_srv_module, 1);
    module->handle = handle;

    if (init(&module->srv, icd_srv_provider_watch_pid, module,
                      icd_srv_provider_close, icd_srv_provider_limited_conn))
    {
      if (module->srv.version)
      {
        if (module->srv.identify)
        {
          if (module->srv.connect)
          {
            if (icd_version_compare(module->srv.version, "0.54") >= 0)
            {
              GSList *nt = data->network_types;

              module->name = g_strdup(module_name);

              for (nt = data->network_types; nt; nt = nt->next )
              {
                const gchar *network_type = (const gchar *)nt->data;

                if (network_type)
                {
                  GSList *nw_type_to_srv;

                  ILOG_INFO("service provider module %p '%s', network type '%s'",
                            module, module->name, network_type);

                  nw_type_to_srv = (GSList *)g_hash_table_lookup(
                        data->icd_ctx->nw_type_to_srv_module, network_type);

                  g_hash_table_insert(data->icd_ctx->nw_type_to_srv_module,
                                      g_strdup(network_type),
                                      g_slist_prepend(nw_type_to_srv, module));
                }
              }

              data->icd_ctx->srv_module_list =
                  g_slist_prepend(data->icd_ctx->srv_module_list, module);
              rv = TRUE;
            }
            else
            {
              ILOG_ERR("Service module '%s' version %s compiled against API < 0.54, not loading it",
                       module_name, module->srv.version);
            }
          }
          else
          {
            ILOG_ERR("Service module '%s' did not have a connect function",
                     module_name);
          }
        }
        else
        {
          ILOG_ERR("Service module '%s' did not have an identify function",
                   module_name);
        }
      }
      else
        ILOG_ERR("Service module '%s' did not set version", module_name);

      if (!rv)
      {
        if (module->srv.srv_destruct)
          module->srv.srv_destruct(&module->srv.private);

        icd_plugin_unload_module(module->handle);
      }
    }
  }
  else
  {
    ILOG_DEBUG("service provider module %p exists for type '%s'", module,
               data->service_type);
    rv = TRUE;
  }

  if (rv)
  {
    g_hash_table_insert(data->icd_ctx->srv_type_to_srv_module,
                        g_strdup(data->service_type), module);
    ILOG_INFO("service provider module %p '%s' version %s srv type '%s'",
              module, module->name, module->srv.version, data->service_type);
  }
  else
  {
    ILOG_WARN("service module %p '%s' failed to load", module, module_name);
    g_free(module);
  }

  return rv;
}

/**
 * @brief  Load service provider modueles
 *
 * @param  icd_ctx  icd context
 *
 * @return TRUE if at least one module got loaded, FALSE otherwise
 */
gboolean
icd_srv_provider_load_modules(struct icd_context *icd_ctx)
{
  GConfClient *gconf = gconf_client_get_default();
  struct icd_srv_provider_cb_data cb_data;
  GSList *dirs;
  GError *err = NULL;
  gboolean rv = FALSE;

  dirs = gconf_client_all_dirs(gconf, ICD_GCONF_SRV_PROVIDERS, &err);

  if (err)
  {
    ILOG_INFO("could not find service provider types: %s", err->message);
    g_clear_error(&err);
    g_object_unref(gconf);
    return FALSE;
  }

  cb_data.icd_ctx = icd_ctx;
  icd_ctx->srv_type_to_srv_module =
      g_hash_table_new_full(g_str_hash,g_str_equal, g_free, NULL);
  icd_ctx->nw_type_to_srv_module =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  while (dirs)
  {
    gchar *dir = (gchar *)dirs->data;

    if (dir)
    {
      GSList *network_types;
      gchar *name;
      gchar *key;
      gchar *filename;
      gchar *service_type = g_strrstr(dir, "/");

      cb_data.service_type = service_type;
      key = g_strconcat(dir, ICD_SRV_PROVIDER_MODULE_NAME, NULL);
      name = gconf_client_get_string(gconf, key, &err);
      g_free(key);

      if (err)
      {
        ILOG_INFO("could not find service provider module name: %s",
                  err->message);
        g_clear_error(&err);
      }

      key = g_strconcat(dir, ICD_SRV_PROVIDER_NETWORK_TYPES, NULL);
      network_types = gconf_client_get_list(gconf, key, GCONF_VALUE_STRING,
                                            &err);
      g_free(key);
      cb_data.network_types = network_types;

      if (err)
      {
        ILOG_WARN("could not find service provider network types: %s",
                  err->message);
        g_clear_error(&err);
      }

      filename = g_strconcat("/usr/lib/icd2", "/", name, NULL);

      if (name && service_type && *service_type && network_types)
      {
        if (icd_plugin_load(filename, name, "icd_srv_init",
                            icd_srv_provider_init, &cb_data))
        {
            rv = TRUE;
        }
      }

      while (network_types)
      {
        g_free(network_types->data);
        network_types = g_slist_delete_link(network_types, network_types);
      }

      g_free(name);
      g_free(filename);
    }

    g_free(dir);
    dirs = g_slist_delete_link(dirs, dirs);
  }

  if (!rv)
    ILOG_INFO("no service provider modules loaded");

  return rv;
}

/**
 * @brief  Wrapper function for removing a list that is a hash table value
 *         when stepping through a hash table
 *
 * @param  key        key; ignored
 * @param  value      the list
 * @param  user_data  user data; ignored
 */
static void
icd_srv_provider_free_list(gpointer key, gpointer value, gpointer user_data)
{
  g_slist_free((GSList *)value);
}

/**
 * @brief  Unload all network modules
 *
 * @param  icd_ctx  icd context
 */
void
icd_srv_provider_unload_modules(struct icd_context *icd_ctx)
{
  GSList *l;

  ILOG_DEBUG("Unloading service provider modules");
  l = icd_ctx->srv_module_list;

  while (l)
  {
    struct icd_srv_module *module = (struct icd_srv_module *)l->data;
    GSList *next = l->next;

    if (module->pid_list)
    {
      ILOG_ERR("Module %s still has child processes running but unloading anyway",
               module->name);
    }

    if (module->srv.srv_destruct)
    {
      ILOG_DEBUG("Calling srv_destruct of module %s", module->name);
      module->srv.srv_destruct(&module->srv.private);
    }
    else
      ILOG_DEBUG("No srv_destruct function in module %s", module->name);

    icd_plugin_unload_module(module->handle);
    g_free(module->name);
    g_free(module);
    icd_ctx->srv_module_list = g_slist_remove_link(icd_ctx->srv_module_list, l);
    l = next;
  }

  if (icd_ctx->srv_type_to_srv_module)
    g_hash_table_destroy(icd_ctx->srv_type_to_srv_module);

  icd_ctx->srv_type_to_srv_module = NULL;

  if (icd_ctx->nw_type_to_srv_module)
  {
    g_hash_table_foreach(icd_ctx->nw_type_to_srv_module,
                         icd_srv_provider_free_list, NULL);
    g_hash_table_destroy(icd_ctx->nw_type_to_srv_module);
  }

  icd_ctx->nw_type_to_srv_module = NULL;
}

/**
 * @brief  Service identification callback.
 *
 * @param  status             status of the identification procedure
 * @param  service_type       service type
 * @param  service_name       name of the service provider displayable to the
 *                            user/UI
 * @param  service_attrs      service attributes
 * @param  service_id         internal service id
 * @param  service_priority   priority within a service type
 * @param  network_type       network type
 * @param  network_attrs      network attributes
 * @param  network_id         network_id
 * @param  identify_cb_token  the token passed to the identification function
 *
 * @todo   this sets the network priority for ALL entries, not just the
 *         service entry; for now it works as the preferred is only WIMAX,
 *         which does not have any networks without a service provider; the
 *         network part is created right here
 */
static void
icd_srv_provider_identify_cb(const enum icd_srv_identify_status status,
                             const gchar *service_type,
                             const gchar *service_name,
                             const guint service_attrs,
                             const gchar *service_id,
                             const gint service_priority,
                             const gchar *network_type,
                             const guint network_attrs,
                             const gchar *network_id,
                             gpointer identify_cb_token)
{
  struct icd_srv_identify *identify =
      (struct icd_srv_identify *)identify_cb_token;
  struct icd_scan_cache_list *scan_cache;
  struct icd_scan_cache *cache_entry = NULL;
  struct icd_scan_srv_provider *provider;
  GSList *l;

  if (!identify)
  {
    ILOG_ERR("identify token is NULL");
    return;
  }

  if (status == ICD_SRV_UNKNOWN)
    goto stop_identify;

  if (!service_type || !service_id)
  {
    ILOG_WARN("cannot accept partial srv info, type '%s' and id '%s'",
              service_type, service_id);
    goto out;
  }

  if (!network_type || !network_id)
  {
    ILOG_WARN("cannot accept partial network info, type '%s' and id '%s'",
              network_type, network_id);
    goto out;
  }

  scan_cache = icd_scan_cache_list_lookup(identify->module, network_id);

  if (!scan_cache)
  {
    ILOG_DEBUG("srv provider created cache list");
    scan_cache = g_new0(struct icd_scan_cache_list, 1);
    goto create_entry;
  }

  cache_entry =
      icd_scan_cache_entry_find(scan_cache, network_type, network_attrs);

  if (!cache_entry)
  {
    cache_entry =
        icd_scan_cache_entry_find(scan_cache, network_type,
                                  network_attrs | ICD_NW_ATTR_SRV_PROVIDER);

    if (!cache_entry)
    {
create_entry:
      cache_entry = g_new0(struct icd_scan_cache, 1);
      cache_entry->network_attrs = network_attrs | ICD_NW_ATTR_SRV_PROVIDER;
      cache_entry->network_type = g_strdup(network_type);
      cache_entry->signal = identify->signal;
      cache_entry->network_id = g_strdup(network_id);
      cache_entry->last_seen = time(NULL);
      cache_entry->network_priority = icd_network_priority_get(service_type,
                                                              service_id,
                                                              network_type,
                                                              network_attrs);
      icd_scan_cache_entry_add(identify->module, scan_cache, cache_entry);
    }
  }

  l = cache_entry->srv_provider_list;

  while (l)
  {
    provider = (struct icd_scan_srv_provider *)l->data;

    if (provider && string_equal(provider->service_type, service_type) &&
        provider->service_attrs == service_attrs &&
        string_equal(provider->service_id, service_id))
    {
      break;
    }

    ILOG_DEBUG("srv provider identified network as %s/%0x/%s, %s/%0x/%s already exists",
               service_type, service_attrs, service_id, provider->service_type,
               provider->service_attrs, provider->service_id);
    l = l->next;
  }

  if (l)
  {
    if (identify->signal > cache_entry->signal ||
        provider->service_priority != service_priority)
    {
      enum icd_scan_status status;

      cache_entry->signal = identify->signal;
      provider->service_priority = service_priority;

      if (identify->signal > cache_entry->signal)
        status = ICD_SCAN_UPDATE;
      else
        status = ICD_SCAN_NOTIFY;

      icd_scan_listener_notify(identify->module, provider, cache_entry, status);
    }
  }
  else
  {
    provider = g_new0(struct icd_scan_srv_provider, 1);
    provider->service_type = g_strdup(service_type);
    provider->service_name = g_strdup(service_name);
    provider->service_attrs = service_attrs;
    provider->service_priority = service_priority;
    provider->service_id = g_strdup(service_id);
    cache_entry->srv_provider_list =
        g_slist_prepend(cache_entry->srv_provider_list, provider);
    icd_scan_listener_notify(identify->module, provider, cache_entry,
                             ICD_SCAN_NEW);
  }

out:
  if (!(status & ICD_SRV_CONTINUE))
    goto stop_identify;

  return;

stop_identify:
    g_free(identify);
}

/**
 * @brief  Send a network to be identified by possible service providers
 *
 * @param  cache_entry  scan cache entry containing the data so far
 * @param  status       scan status
 *
 * @return TRUE if at least one service module was called, FALSE if not
 */
gboolean
icd_srv_provider_identify(struct icd_network_module *nw_module,
                          struct icd_scan_cache *cache_entry,
                          enum icd_scan_status status)
{
  GSList *l;
  gboolean rv = FALSE;

  l = (GSList *)g_hash_table_lookup(icd_context_get()->nw_type_to_srv_module,
                                    cache_entry->network_type);

  for (; l; l = l->next)
  {
    struct icd_srv_module *module = (struct icd_srv_module *)l->data;
    if (module)
    {
      if (module->srv.identify)
      {
        struct icd_srv_identify *identify = g_new0(struct icd_srv_identify, 1);

        rv = TRUE;
        identify->module = nw_module;
        identify->signal = cache_entry->signal;
        module->srv.identify(status, cache_entry->network_type,
                             cache_entry->network_name,
                             cache_entry->network_attrs,
                             cache_entry->network_id,
                             cache_entry->network_priority,
                             cache_entry->signal,
                             cache_entry->station_id, cache_entry->dB,
                             icd_srv_provider_identify_cb, identify,
                             &module->srv.private);
      }
    }
    else
    {
      ILOG_WARN("service module NULL for nw type '%s'",
                cache_entry->network_type);
    }

  }

  return rv;
}

/**
 * @brief  Check whether there is a service module to run
 *
 * @param  iap  the IAP
 *
 * @return TRUE if there is a service module to run, FALSE otherwise
 */
gboolean
icd_srv_provider_has_next(struct icd_iap *iap)
{
  if (iap->connection.service_type && iap->connection.service_id)
    return iap->srv_connect_cb == NULL;

  ILOG_DEBUG("no service module for iap %p since service type or service id is empty: '%s'/'%s'",
             iap, iap->connection.service_type, iap->connection.service_id);

  return FALSE;
}

/**
 * @brief  Connect callback for the service provider module
 *
 * @param  status            status of the connect attempt; with
 *                           ICD_SRV_RESTART the IAP will be disconnected and
 *                           reconnected again
 * @param  connect_cb_token  token passed to the connect function
 */
static void
icd_srv_provider_connect_cb(enum icd_srv_status status, const gchar *err_str,
                            gpointer connect_cb_token)
{

  struct icd_iap *iap = (struct icd_iap *)connect_cb_token;

  if (iap)
  {
    icd_srv_provider_connect_cb_fn connect_cb =
        (icd_srv_provider_connect_cb_fn)iap->srv_connect_cb;

    if (connect_cb)
      connect_cb(status, err_str, iap->srv_connect_cb_user_data);

    iap->srv_connect_cb_user_data = NULL;
    iap->srv_connect_cb = NULL;
  }
  else
    ILOG_ERR("srv provider connecte cb returned NULL iap");
}

/**
 * @brief  Run the service module, if any
 *
 * @param  iap        the IAP
 * @param  cb         callback function
 * @param  user_data  user data for the callback function
 *
 * @return TRUE if the service module got called, FALSE if not
 */
gboolean
icd_srv_provider_connect(struct icd_iap *iap, icd_srv_provider_connect_cb_fn cb,
                         gpointer user_data)
{
  struct icd_context *icd_ctx = icd_context_get();
  struct icd_srv_module *module;

  if (!cb)
  {
    ILOG_ERR("srv provider connect cb cannot be NULL");
    return FALSE;
  }

  if (!icd_srv_provider_has_next(iap))
  {
    ILOG_INFO("srv provider connect already in progress for iap %p", iap);
    return FALSE;
  }

  module = (struct icd_srv_module *)g_hash_table_lookup(
        icd_ctx->srv_type_to_srv_module, iap->connection.service_type);

  if (!module)
  {
    ILOG_ERR("srv type '%s' unknown", iap->connection.service_type);

    return FALSE;

  }

  iap->srv_connect_cb = cb;
  iap->srv_connect_cb_user_data = user_data;
  module->srv.connect(iap->connection.service_type,
                      iap->connection.service_attrs, iap->connection.service_id,
                      iap->connection.network_type,
                      iap->connection.network_attrs, iap->connection.network_id,
                      iap->interface_name, icd_srv_provider_connect_cb, iap,
                      &module->srv.private);
  return TRUE;
}
