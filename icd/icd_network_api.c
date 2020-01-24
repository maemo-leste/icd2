/**
@file icd_network_api.c
@copyright GNU GPLv2 or later

@addtogroup icd_network_api ICd network API handling
@ingroup internal

 * @{ */

#include <string.h>
#include <gconf/gconf-client.h>
#include <osso-ic-dbus.h>
#include "icd_log.h"
#include "icd_context.h"
#include "icd_plugin.h"
#include "icd_scan.h"
#include "icd_srv_provider.h"
#include "icd_iap.h"
#include "icd_network_api.h"
#include "icd_type_modules.h"
#include "icd_status.h"
#include "config.h"
#include "icd_version.h"

/** prefix for the ICd network API modules */
#define ICD_NW_API_PREFIX   "libicd_network_"

/** name of the ICd network API init function */
#define ICD_NW_INIT   "icd_nw_init"

/**
 * Iterate over all network modules
 *
 * @param icd_ctx     icd context
 * @param foreach_fn  the function to call for each module
 * @param user_data   user data to pass to the function
 *
 * @return  a pointer to the module if the iteration function returns FALSE;
 *          NULL otherwise
 * @todo  make icd_scan use the module iteration function
 */
struct icd_network_module *
icd_network_api_foreach_module(struct icd_context *icd_ctx,
                               icd_network_api_foreach_module_fn foreach_fn,
                               gpointer user_data)
{
  GSList *l;

  if (!icd_ctx || !foreach_fn)
  {
    ILOG_ERR("icd_ctx or foreach_fn cannot be NULL");
    return NULL;
  }

  for (l = icd_ctx->nw_module_list; l; l = l->next)
  {
    struct icd_network_module *module = (struct icd_network_module *)l->data;

    if (l->data)
    {
      if (!foreach_fn(module, user_data))
        return module;
    }
    else
      ILOG_WARN("Module list has NULL module data");
  }

  return NULL;
}

/** pid and exit value structure */
struct pid_notify {
  /** process id */
  pid_t pid;
  /** exit value  */
  gint exit_value;
};

/**
 * Find the network module that is watching a child process exit
 *
 * @param module     the network module to examine
 * @param user_data  the pid_notify structure
 *
 * @return  TRUE to continue searching, FALSE to exit iteration and return a
 *          pointer to the module
 */
static gboolean
icd_network_api_foreach_module_pid(struct icd_network_module *module,
                                   gpointer user_data)
{
  struct pid_notify *notify = (struct pid_notify *)user_data;
  GSList *l;
  struct pid_notify *candidate;

  for (l = module->pid_list; l; l = l->next)
  {
    candidate = (struct pid_notify *)l->data;

    if (candidate->pid == notify->pid)
      break;
  }

  if (!l)
    return TRUE;

  g_free(candidate);
  module->pid_list = g_slist_delete_link(module->pid_list, l);

  if (module->nw.child_exit)
  {
    ILOG_INFO("module '%s' notified for pid %d", module->name, notify->pid);
    module->nw.child_exit(notify->pid, notify->exit_value, &module->nw.private);
  }
  else
  {
    ILOG_WARN("module '%s' cannot be notified about pid %d as child_exit is NULL",
              module->name, notify->pid);
  }

  return FALSE;
}

/**
 * Notify a network module that its child process has exited
 *
 * @param icd_ctx     the context
 * @param pid         the process id
 * @param exit_value  exit value
 *
 * @return  TRUE if the process id was in use by the network api; FALSE if
 *          not
 */
gboolean
icd_network_api_notify_pid(struct icd_context *icd_ctx, const pid_t pid,
                           const gint exit_value)
{
  struct pid_notify notify;
  notify.pid = pid;
  notify.exit_value = exit_value;

  return icd_network_api_foreach_module(icd_ctx,
                                        icd_network_api_foreach_module_pid,
                                        &notify) != 0;
}

/**
 * Set ICd to watch a child pid
 * @param pid             process id
 * @param watch_cb_token  the watch callback token given on initialization
 */
static void
icd_network_api_watch_pid(const pid_t pid, gpointer watch_cb_token)
{
  struct icd_network_module *module;
  pid_t *ppid;

  module = (struct icd_network_module *)watch_cb_token;

  if (module)
  {
    ppid = g_new(pid_t, 1);
    *ppid = pid;
    module->pid_list = g_slist_prepend(module->pid_list, ppid);

    ILOG_DEBUG("added pid %d to module '%s'", pid, module->name);
  }
  else
    ILOG_ERR("module NULL while submitting child pid");
}

/**
 * Status of the network has changed while the network has been connected
 *
 * @param network_type   the type of the IAP returned
 * @param network_attrs  attributes, such as type of network_id, security,
 *                       etc.
 * @param network_id     IAP name or local id, e.g. SSID
 */
static void
icd_network_api_status_update(gchar *network_type, guint network_attrs,
                              gchar *network_id)
{
  struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);

  if (!iap)
  {
    ILOG_WARN("status update requested for %s/%0x/%s, but no matching IAP",
              network_type, network_attrs, network_id);
    return;
  }

  if (iap->state == ICD_IAP_STATE_CONNECTED)
  {
    ILOG_INFO("status update requested for IAP %p, %s/%0x/%s", iap,
              network_type, network_attrs, network_id);
    icd_status_connected(iap, NULL, NULL);
  }
  else
  {
    ILOG_WARN("status update requested for %s/%0x/%s, but IAP not in state ICD_IAP_STATE_CONNECTED",
              network_type, network_attrs, network_id);
  }
}

/**
 * Function for closing down a connection by request of a network module
 *
 * @param status         reason for closing; #ICD_NW_RESTART if the IAP needs
 *                       to be restarted, success or error will both close
 *                       the network connection
 * @param err_str        NULL if the network was disconnected normally or any
 *                       ICD_DBUS_ERROR_* from osso-ic-dbus.h on error
 * @param network_type   the type of the IAP returned
 * @param network_attrs  attributes, such as type of network_id, security,
 *                       etc.
 * @param network_id     IAP name or local id, e.g. SSID
 */
static void
icd_network_api_close(enum icd_nw_status status, const gchar *err_str,
                      const gchar *network_type, const guint network_attrs,
                      const gchar *network_id)
{
  struct icd_iap *iap =icd_iap_find(network_type, network_attrs, network_id);

  if (!iap)
  {
    ILOG_WARN("disconnect requested for %s/%0x/%s, but no matching IAP",
              network_type, network_attrs, network_id);
    return;
  }

  if (iap->state >= ICD_IAP_STATE_CONNECTED_DOWN &&
      iap->state <= ICD_IAP_STATE_SCRIPT_POST_DOWN)
  {
    ILOG_INFO("close requested for IAP %p ignored, already in state %s", iap,
              icd_iap_state_names[iap->state]);
    return;
  }

  ILOG_INFO("close requested for IAP %p, %s/%0x/%s", iap, network_type,
            network_attrs, network_id);
  icd_status_disconnect(iap, 0, err_str);

  switch (status)
  {
    case ICD_NW_RESTART:
      icd_iap_restart(iap, ICD_NW_LAYER_ALL);
      break;
    case ICD_NW_RESTART_IP:
      icd_iap_restart(iap, ICD_NW_LAYER_IP);
      break;
    case ICD_NW_RESTART_LINK_POST:
      icd_iap_restart(iap, ICD_NW_LAYER_LINK_POST);
      break;
    case ICD_NW_RESTART_LINK:
      icd_iap_restart(iap, ICD_NW_LAYER_LINK);
      break;
    default:
    {
      const char *err;

      if (status == ICD_NW_ERROR && err_str)
        err = err_str;
      else
        err = ICD_DBUS_ERROR_NETWORK_ERROR;

      icd_iap_disconnect(iap, err);
      break;
    }
  }
}

/**
 * Request a network module layer to be renewed
 *
 * @param renew_layer    the network module layer to renew
 * @param network_type   network type
 * @param network_attrs  network_attrs
 * @param network_id     network_id
 *
 * @todo  when saving, the dialog might try to save a name for a failed
 *        iap...
 */
static void
icd_network_api_renew(enum icd_nw_layer renew_layer, const gchar *network_type,
                      const guint network_attrs, const gchar *network_id)
{
  struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);

  if (!iap)
  {
    ILOG_WARN("network renew requested for %s/%0x/%s, but no matching IAP",
              network_type, network_attrs, network_id);
  }

  ILOG_DEBUG("network renew for iap %p %s/%0x/%s layer %d requested", iap,
             network_type, network_attrs, network_id, renew_layer);

  switch (iap->state)
  {
    case ICD_IAP_STATE_DISCONNECTED:
    case ICD_IAP_STATE_SCRIPT_PRE_UP:
      ILOG_WARN("cannot renew anything in state %s",
                icd_iap_state_names[iap->state]);
      return;
    case ICD_IAP_STATE_LINK_UP:
      if (renew_layer == ICD_NW_LAYER_NONE ||
          renew_layer == ICD_NW_LAYER_LINK || renew_layer == ICD_NW_LAYER_ALL)
      {
        icd_iap_restart(iap, renew_layer);
        return;
      }

      break;
    case ICD_IAP_STATE_LINK_POST_UP:
      if (renew_layer == ICD_NW_LAYER_LINK_POST)
        icd_iap_restart(iap, renew_layer);
      else if (renew_layer == ICD_NW_LAYER_NONE ||
               renew_layer == ICD_NW_LAYER_LINK ||
               renew_layer == ICD_NW_LAYER_ALL)
      {
        icd_iap_renew(iap, renew_layer);
      }
      else
        break;

      return;
    case ICD_IAP_STATE_IP_UP:
      if (renew_layer == ICD_NW_LAYER_IP)
        icd_iap_restart(iap, renew_layer);
      else if (renew_layer != ICD_NW_LAYER_SERVICE)
        icd_iap_renew(iap, renew_layer);
      else
          break;

      return;
    case ICD_IAP_STATE_SRV_UP:
      if (renew_layer != ICD_NW_LAYER_SERVICE)
        icd_iap_renew(iap, renew_layer);
      else
        icd_iap_restart(iap, renew_layer);

      return;
    case ICD_IAP_STATE_SCRIPT_POST_UP:
    case ICD_IAP_STATE_SAVING:
    case ICD_IAP_STATE_CONNECTED:
      icd_iap_renew(iap, renew_layer);
      return;
    default:
      ILOG_DEBUG("no need to renew anything in state %s while going down",
                 icd_iap_state_names[iap->state]);
      return;
  }

  ILOG_DEBUG("in state %s, no need to renew anything",
             icd_iap_state_names[iap->state]);
}

/**
 * Function for checking whether a modules supports a given type
 *
 * @param module  the module
 * @param type    the type to check for
 *
 * @return        TRUE if the module supports the given type, FALSE otherwise
 */
gboolean
icd_network_api_has_type(struct icd_network_module *module, const gchar *type)
{
  struct icd_context *icd_ctx;
  GSList *l;

  if (!type || !module)
    return FALSE;

  icd_ctx = icd_context_get();

  for (l = (GSList *)g_hash_table_lookup(icd_ctx->type_to_module, type); l;
       l = l->next)
  {
    if (l->data)
    {
      if (l->data == module)
        return TRUE;
    }
    else
      ILOG_CRIT("Module list has NULL type");
  }

  return FALSE;
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
 */
static gboolean
icd_network_api_init_cb(const gchar *module_name, void *handle,
                        gpointer init_function, gpointer data)
{
  struct icd_context *icd_ctx = (struct icd_context *)data;
  struct icd_network_module *module = g_new0(struct icd_network_module, 1);

  module->handle = handle;

  if (!((icd_nw_init_fn)init_function)(&module->nw, icd_network_api_watch_pid,
                                       module, icd_network_api_close,
                                       icd_network_api_status_update,
                                       icd_network_api_renew))
  {
    goto err_init;
  }

  if (!module->nw.version)
  {
    ILOG_ERR("Module '%s' did not set version", module_name);
    goto err_version;
  }

  if (icd_version_compare(module->nw.version, PACKAGE_VERSION) > 0)
  {
    ILOG_ERR("module '%s' version %s is greater than " PACKAGE_TARNAME
             " version " PACKAGE_VERSION ", not loading it",
             module_name, module->nw.version);
    goto err_version;
  }

  if (icd_version_compare(module->nw.version, "0.25") < 0 &&
      (module->nw.ip_addr_info || module->nw.ip_stats ||
       module->nw.link_post_stats || module->nw.link_stats ||
       module->nw.stop_search))
  {
    ILOG_ERR("module '%s' version %s compiled against API < 0.25, not loading it",
             module_name, module->nw.version);
    goto err_version;
  }

  if (icd_version_compare(module->nw.version, "0.26") < 0 &&
      (module->nw.ip_up || module->nw.link_post_up || module->nw.link_up))
  {
    ILOG_ERR("module '%s' version %s compiled against API < 0.26, not loading it",
             module_name, module->nw.version);
    goto err_version;
  }

  if (icd_version_compare(module->nw.version, "0.37") < 0)
  {
    if (module->nw.start_search)
    {
      ILOG_ERR("module '%s' version %s compiled against API < 0.37, not loading it",
               module_name, module->nw.version);
      goto err_version;
    }
  }
  else if (module->nw.start_search &&
           module->nw.search_lifetime <= module->nw.search_interval)
  {
    ILOG_ERR("module '%s' search lifetime (%d) must be greater than search interval (%d)",
             module_name, module->nw.search_lifetime,
             module->nw.search_interval);
    goto err_version;
  }

  ILOG_DEBUG("Network module %p '%s' version %s", module, module_name,
             module->nw.version);

  module->name = g_strdup(module_name);
  icd_ctx->nw_module_list = g_slist_prepend(icd_ctx->nw_module_list, module);

  if (module->nw.start_search)
    icd_scan_cache_init(module);

  return TRUE;

err_version:
  if (module->nw.network_destruct)
    module->nw.network_destruct(&module->nw.private);

  icd_plugin_unload_module(module->handle);

err_init:
  ILOG_WARN("network module %p '%s' failed to load", module, module_name);
  g_free(module);

  return FALSE;
}

/**
 * Find a network module by its name
 * @param module_name  module name
 * @return  the module structure or NULL if not found
 */
static struct icd_network_module*
icd_network_api_find_module(gchar *module_name)
{
  GSList *module_list = icd_context_get()->nw_module_list;

  while (module_list)
  {
    struct icd_network_module *module =
        (struct icd_network_module *)module_list->data;

    if (!strcmp(module->name, module_name))
      return module;

    module_list = module_list->next;
  }

  return NULL;
}

/**
 * Load all network API modules
 * @param icd_ctx  icd context
 * @return  the status from icd_plugin_load_all
 */
gboolean
icd_network_api_load_modules(struct icd_context *icd_ctx)
{
  gchar *network_type;
  GSList *modules;
  GConfClient *gconf;
  GSList *dirs;
  gchar *dir;
  gboolean rv = FALSE;
  GError *err = NULL;

  if (!icd_plugin_load_all("/usr/lib/icd2", ICD_NW_API_PREFIX, ICD_NW_INIT,
                           icd_network_api_init_cb, icd_ctx))
  {
    return FALSE;
  }

  icd_ctx->type_to_module =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  gconf = gconf_client_get_default();
  dirs = gconf_client_all_dirs(gconf, ICD_GCONF_NETWORK_MAPPING, &err);

  if (err)
  {
    ILOG_ERR("could not find network type to module mappings: %s",
             err->message);
    g_clear_error(&err);
    g_object_unref(gconf);
    return FALSE;
  }

  if (!dirs)
  {
    g_object_unref(gconf);
    goto err_out;
  }

  do
  {
    dir = (gchar *)dirs->data;
    network_type = g_strrstr(dir, "/");

    if (network_type)
      network_type++;

    modules = icd_network_modules_get(network_type);

    if (modules)
    {
      GSList *type_to_module = NULL;
      gboolean all_found = TRUE;

      while (modules)
      {
        gchar *module_name = (gchar *)modules->data;
        struct icd_network_module *module =
                   icd_network_api_find_module(module_name);

        if (module)
        {
          ILOG_DEBUG("network type '%s' uses module '%s'",
                     network_type, module_name);
          type_to_module = g_slist_append(type_to_module, module);
        }
        else
        {
          ILOG_DEBUG("network type '%s' could not find module '%s'",
                     network_type, module_name);
          all_found = FALSE;
        }

        g_free(module_name);
        modules = g_slist_delete_link(modules, modules);
      }

      if (all_found)
      {
        GSList *tmpl = type_to_module;

        g_hash_table_insert(icd_ctx->type_to_module, g_strdup(network_type),
                            type_to_module);

        while (tmpl)
        {
          struct icd_network_module *mod =
              (struct icd_network_module *)tmpl->data;

          mod->network_types = g_slist_prepend(mod->network_types,
                                               g_strdup(network_type));
          tmpl = tmpl->next;
        }

        rv = TRUE;
      }
      else
        g_slist_free(type_to_module);
    }
    else
      ILOG_ERR("network type '%s' has no modules defined", network_type);

    g_free(dir);
    dirs = g_slist_delete_link(dirs, dirs);
  }
  while (dirs);

  g_object_unref(gconf);

  if (!rv)
    goto err_out;

  return rv;

err_out:
  ILOG_CRIT("could not map any network type to network modules");

  return FALSE;
}

/**
 * Unload all network modules
 * @param icd_ctx  icd context
 */
void
icd_network_api_unload_modules(struct icd_context *icd_ctx)
{
  GSList *l = icd_ctx->nw_module_list;;

  ILOG_DEBUG("Unloading network api modules");

  while (l)
  {
    struct icd_network_module *module = (struct icd_network_module *)l->data;
    GSList *next = l->next;

    if (module->pid_list)
    {
      ILOG_ERR("Module %s still has child processes running but unloading anyway ",
               module->name);
    }

    if (module->nw.network_destruct )
    {
      ILOG_DEBUG("Calling network_destruct of module %s", module->name);
      module->nw.network_destruct(&module->nw.private);
    }
    else
      ILOG_DEBUG("No network_destruct function in module %s", module->name);

    icd_scan_cache_remove(module);
    icd_plugin_unload_module(module->handle);

    while (module->network_types)
    {
      ILOG_DEBUG("removing network type '%s' from '%s'",
                 (gchar *)module->network_types->data, module->name);
      g_free(module->network_types->data);
      module->network_types =
          g_slist_delete_link(module->network_types, module->network_types);
    }

    g_free(module->name);
    g_free(module);
    icd_ctx->nw_module_list = g_slist_remove_link(icd_ctx->nw_module_list, l);
    l = next;
  }

  if (icd_ctx->type_to_module)
    g_hash_table_destroy(icd_ctx->type_to_module);
}

/** @} */
