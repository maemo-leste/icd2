/**
@file icd_iap.c
@copyright GNU GPLv2 or later

@addtogroup icd_iap IAP connection abstraction
@ingroup internal

 * @{ */

#include <sys/types.h>
#include <unistd.h>
#include <glib.h>
#include <string.h>
#include <gconf/gconf-client.h>
#include <osso-ic-dbus.h>
#include <osso-ic-gconf.h>
#include "icd_iap.h"
#include "icd_network_api.h"
#include "icd_log.h"
#include "icd_context.h"
#include "icd_policy_api.h"
#include "icd_idle_timer.h"
#include "icd_request.h"
#include "icd_script.h"
#include "network_api.h"
#include "icd_gconf.h"
#include "icd_osso_ic.h"
#include "icd_status.h"
#include "icd_srv_provider.h"
#include "icd_dbus_api.h"

static gboolean icd_iap_run_restart(struct icd_iap *iap);
static gboolean icd_iap_run_renew(struct icd_iap *iap);
static void icd_iap_run_post_down_scripts(struct icd_iap *iap);
static void icd_iap_module_next(struct icd_iap *iap);

static void icd_iap_disconnect_cb(const enum icd_nw_status status,
                                  const gpointer cb_token);

static void icd_iap_srv_disconnect_cb(enum icd_srv_status status,
                                      gpointer disconnect_cb_token);

static void icd_iap_post_up_script_done(const pid_t pid,
                                        const gint exit_value,
                                        gpointer user_data);

/** names for the different states */
const gchar *icd_iap_state_names[ICD_IAP_MAX_STATES] = {
  "ICD_IAP_STATE_DISCONNECTED",
  "ICD_IAP_STATE_SCRIPT_PRE_UP",
  "ICD_IAP_STATE_LINK_UP",
  "ICD_IAP_STATE_LINK_POST_UP",
  "ICD_IAP_STATE_IP_UP",
  "ICD_IAP_STATE_SRV_UP",
  "ICD_IAP_STATE_SCRIPT_POST_UP",
  "ICD_IAP_STATE_SAVING",
  "ICD_IAP_STATE_CONNECTED",
  "ICD_IAP_STATE_CONNECTED_DOWN",
  "ICD_IAP_STATE_SRV_DOWN",
  "ICD_IAP_STATE_IP_DOWN",
  "ICD_IAP_STATE_IP_RESTART_SCRIPTS",
  "ICD_IAP_STATE_LINK_PRE_DOWN",
  "ICD_IAP_STATE_LINK_PRE_RESTART_SCRIPTS",
  "ICD_IAP_STATE_LINK_DOWN",
  "ICD_IAP_STATE_LINK_RESTART_SCRIPTS",
  "ICD_IAP_STATE_SCRIPT_POST_DOWN"
};

/** names for status codes */
static const gchar *icd_iap_status_names[] =
 {
  "ICD_IAP_CREATED",
  "ICD_IAP_DISCONNECTED",
  "ICD_IAP_BUSY",
  "ICD_IAP_FAILED"
};

/** names for network module layers */
static const gchar *icd_iap_layer_names[] = {
  "ICD_NW_LAYER_NONE",
  "ICD_NW_LAYER_LINK",
  "ICD_NW_LAYER_LINK_POST",
  "ICD_NW_LAYER_IP",
  "ICD_NW_LAYER_SERVICE",
  "ICD_NW_LAYER_ALL"
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
 * Iterate over all active IAPs
 *
 * @param fn         function to call for each IAP
 * @param user_data  user data to pass to the iterator function
 *
 * @return  the IAP struct where fn returns FALSE, NULL otherwise or on error
 */
struct icd_iap *
icd_iap_foreach(icd_iap_foreach_fn fn, gpointer user_data)
{
  struct icd_context *icd_ctx;
  GSList *l;

  if (!fn)
  {
    ILOG_ERR("iap iterator function NULL");
    return NULL;
  }

  icd_ctx = icd_context_get();
  l = icd_ctx->request_list;

  if (!l)
    return NULL;

  for (l = icd_ctx->request_list; l; l = l->next)
  {
    GSList *iaps;
    struct icd_request *request = (struct icd_request *)l->data;

    if (!request)
    {
      ILOG_ERR("request in list is NULL");
      continue;
    }

    iaps = request->try_iaps;

    if (!iaps)
    {
      ILOG_DEBUG("request %p has no IAPs", request);
      continue;
    }

    if (iaps->data)
    {
      struct icd_iap *iap = (struct icd_iap *)iaps->data;

      if (!fn(iap, user_data))
        return iap;
    }
    else
      ILOG_ERR("request %p has NULL iap in list", request);
  }

  return NULL;
}

/**
 * Restart a network module by disconnecting network modules including the
 * requested layer. When the requested layer has been disconnected, reconnect
 * starting from the requested layer.
 *
 * @param iap          the IAP
 * @param renew_layer  the layer which is to be disconnectd
 */
void
icd_iap_restart(struct icd_iap *iap, enum icd_nw_layer restart_layer)
{
  if (iap->state <= ICD_IAP_STATE_CONNECTED ||
      iap->state == ICD_IAP_STATE_IP_RESTART_SCRIPTS ||
      iap->state == ICD_IAP_STATE_LINK_PRE_RESTART_SCRIPTS ||
      iap->state == ICD_IAP_STATE_LINK_RESTART_SCRIPTS)

  {
    ILOG_INFO("restarting iap %p layer %s", iap,
              icd_iap_layer_names[restart_layer]);

    iap->restart_layer = restart_layer;
    iap->restart_state = iap->state;
    icd_iap_disconnect(iap, NULL);
  }
  else
    ILOG_INFO("ignored restart for iap %p since already disconnecting", iap);
}

/**
 * Call all network module _down functions added to the IAP. This function
 * does not handle cancelled IAPs which have not yet called their respective
 * _up functions and can't thus be merged with icd_iap_disconnect().
 *
 * @param iap  IAP to disconnect
 */
static void
icd_iap_disconnect_module(struct icd_iap *iap)
{
  switch (iap->state)
  {
    case ICD_IAP_STATE_SRV_UP:
    case ICD_IAP_STATE_CONNECTED_DOWN:
      iap->state = ICD_IAP_STATE_SRV_DOWN;
    case ICD_IAP_STATE_SRV_DOWN:
      if (iap->limited_conn)
      {
        iap->limited_conn = FALSE;
        icd_status_limited_conn(iap, NULL, NULL);
      }

      if (icd_srv_provider_disconnect(iap, icd_iap_srv_disconnect_cb, iap))
      {
        ILOG_INFO("called srv disconnect function");
        return;
      }

      ILOG_INFO("No srv module to call");
    case ICD_IAP_STATE_IP_UP:
      iap->state = ICD_IAP_STATE_IP_DOWN;
    case ICD_IAP_STATE_IP_DOWN:
    {
      GSList *l = iap->ip_down_list;

      if (l)
      {
        struct icd_iap_disconnect_data *data =
            (struct icd_iap_disconnect_data *)l->data;

        if (data)
        {
          icd_nw_ip_down_fn function = (icd_nw_ip_down_fn)data->function;

          iap->ip_down_list = g_slist_delete_link(l, iap->ip_down_list);

          if (function)
          {
            ILOG_INFO("calling ip_down function %p", function);
            function(iap->connection.network_type,
                     iap->connection.network_attrs,
                     iap->connection.network_id,
                     iap->interface_name, icd_iap_disconnect_cb, iap,
                     data->private);
            g_free(data);
            return;
          }
        }
        else
          iap->ip_down_list = g_slist_delete_link(l, iap->ip_down_list);

        ILOG_ERR("ip_down function is NULL");
        g_free(data);
      }
      else
      {
        ILOG_INFO("no more ip_down functions to call");

        if (icd_iap_run_restart(iap))
          return;
      }
    }
    case ICD_IAP_STATE_LINK_POST_UP:
      iap->state = ICD_IAP_STATE_LINK_PRE_DOWN;
    case ICD_IAP_STATE_LINK_PRE_DOWN:
    {
      GSList *l = l = iap->link_pre_down_list;

      if (l)
      {
        struct icd_iap_disconnect_data *data =
            (struct icd_iap_disconnect_data *)l->data;

        if (data)
        {
          icd_nw_link_pre_down_fn function =
              (icd_nw_link_pre_down_fn)data->function;

          iap->link_pre_down_list =
              g_slist_delete_link(l, iap->link_pre_down_list);

          if (function)
          {
            ILOG_INFO("calling link_pre_down function %p", function);
            function(iap->connection.network_type,
                     iap->connection.network_attrs,
                     iap->connection.network_id,
                     iap->interface_name, icd_iap_disconnect_cb, iap,
                     data->private);
            g_free(data);
            return;
          }
        }
        else
        {
          iap->link_pre_down_list =
              g_slist_delete_link(l, iap->link_pre_down_list);
        }

        ILOG_ERR("link_pre_down function is NULL");
        g_free(data);
      }
      else
      {
        ILOG_INFO("no more link_pre_down functions to call");

        if (icd_iap_run_restart(iap))
          return;
      }
    }
    case ICD_IAP_STATE_LINK_UP:
      iap->state = ICD_IAP_STATE_LINK_DOWN;
    case ICD_IAP_STATE_LINK_DOWN:
    {
      GSList *l = iap->link_down_list;

      if (l)
      {
        struct icd_iap_disconnect_data *data =
            (struct icd_iap_disconnect_data *)l->data;

        if (data)
        {
          icd_nw_link_down_fn function = (icd_nw_link_down_fn)data->function;

          iap->link_down_list = g_slist_delete_link(l, iap->link_down_list);

          if (function)
          {
            ILOG_INFO("calling link_down function %p", function);
            function(iap->connection.network_type,
                     iap->connection.network_attrs,
                     iap->connection.network_id,
                     iap->interface_name, icd_iap_disconnect_cb, iap,
                     data->private);
            g_free(data);
            return;
          }
        }
        else
          iap->link_down_list = g_slist_delete_link(l, iap->link_down_list);

        ILOG_ERR("link_down function is NULL");
        g_free(data);
      }
      else
      {
        ILOG_INFO("no more link_down functions to call");

        if (icd_iap_run_restart(iap))
          return;
      }
    }
    case ICD_IAP_STATE_SCRIPT_PRE_UP:
      iap->state = ICD_IAP_STATE_SCRIPT_POST_DOWN;
    case ICD_IAP_STATE_SCRIPT_POST_DOWN:
      icd_iap_run_post_down_scripts(iap);
      return;
    default:
      ILOG_ERR("IAP in wrong state %s while disconnecting",
               icd_iap_state_names[iap->state]);
      return;
  }
}

/**
 * Disconnect callback function for all IAP network _down functions
 * @param status    the status of the _down function, ignored mostly for now
 * @param cb_token  the IAP
 */
static void
icd_iap_disconnect_cb(const enum icd_nw_status status, const gpointer cb_token)
{
  struct icd_iap *iap = (struct icd_iap *)cb_token;
  enum icd_iap_state state = iap->state;

  if (state == ICD_IAP_STATE_SRV_DOWN || state == ICD_IAP_STATE_IP_DOWN ||
      state == ICD_IAP_STATE_LINK_PRE_DOWN || state == ICD_IAP_STATE_LINK_DOWN)
  {
    ILOG_DEBUG("IAP %p in state %s disconnected", iap,
               icd_iap_state_names[iap->state]);
    icd_iap_disconnect_module(iap);
  }
  else
    ILOG_ERR("IAP disconnect cb called in state %s",
             icd_iap_state_names[iap->state]);
}

/**
 * Pre-down script callback to remove script pid from list.
 *
 * @param pid         the process id of the script that exited
 * @param exit_value  exit value of the script or -1 on timeout
 * @param user_data   IAP
 */
static void
icd_iap_pre_down_script_done(pid_t pid, gint exit_value, gpointer user_data)
{
  struct icd_iap *iap = (struct icd_iap *)user_data;

  iap->script_pids = g_slist_remove(iap->script_pids, GINT_TO_POINTER(pid));

  if (iap->script_pids)
    ILOG_INFO("still more pre-down scripts to come, waiting");
  else
    icd_iap_disconnect_module(iap);
}

/**
 * Helper function for pre down script to check whether there is another
 * connected IAP
 *
 * @param iap        IAP to examine
 * @param user_data  IAP that is going down
 *
 * @return  TRUE to continue iterating, FALSE to stop and return the IAP
 */
static gboolean
icd_iap_check_connected(struct icd_iap *iap, gpointer user_data)
{
  if (iap == user_data)
    return TRUE;

  return iap->state != ICD_IAP_STATE_CONNECTED;
}

/**
 * Disconnect callback for the service provider module
 * @param status               status of the disconnect, ignored for now
 * @param disconnect_cb_token  token passed to the disconnect function
 */
static void
icd_iap_srv_disconnect_cb(enum icd_srv_status status,
                          gpointer disconnect_cb_token)
{
  icd_iap_disconnect_module((struct icd_iap *)disconnect_cb_token);
}

/**
 * Call pre-down network scripts.
 * @param iap  the IAP
 */
static void
icd_iap_script_pre_down(struct icd_iap *iap)
{
  gchar *iap_id = NULL;
  gboolean remove_proxies;
  GSList *script_env;

  while (iap->script_pids)
  {
    pid_t pid = GPOINTER_TO_INT(iap->script_pids->data);
    ILOG_DEBUG("requesting cancellation of script pid %d", pid);
    icd_script_cancel(pid);
    iap->script_pids = g_slist_delete_link(iap->script_pids,
                                           iap->script_pids);
  }

  if (iap->id && !iap->id_is_local)
    iap_id = gconf_escape_key(iap->id, -1);

  remove_proxies = !icd_iap_foreach(icd_iap_check_connected, iap);
  script_env = iap->script_env;

  if (script_env)
  {
    for (; script_env; script_env = script_env->next)
    {
      if (script_env->data)
      {
        const struct icd_iap_env *env = script_env->data;
        pid_t pid = icd_script_pre_down(iap->interface_name, iap_id,
                                        iap->connection.network_type,
                                        remove_proxies,
                                        env,
                                        icd_iap_pre_down_script_done,
                                        iap);
        iap->script_pids = g_slist_prepend(iap->script_pids,
                                           (gpointer)(intptr_t)pid);
      }
    }
  }
  else
  {
    ILOG_INFO("no env vars for pre-down script");

    pid_t pid = icd_script_pre_down(iap->interface_name, iap_id,
                                    iap->connection.network_type,
                                    remove_proxies, NULL,
                                    icd_iap_pre_down_script_done,
                                    iap);
    iap->script_pids = g_slist_prepend(iap->script_pids,
                                       (gpointer)(intptr_t)pid);
  }

  g_free(iap_id);
}

/**
 * Start disconnecting the current connecting module if it has not yet called
 * it's callback. Set the state to _down so that the IAP cannot be
 * disconnected again.
 *
 * @param iap      IAP
 * @param err_str  NULL if the network was disconnected normally or any
 *                 ICD_DBUS_ERROR_* from osso-ic-dbus.h on error
 */
void
icd_iap_disconnect(struct icd_iap *iap, const gchar *err_str)
{
  ILOG_INFO("disconnect requested for IAP %p in state %s, %s", iap,
            icd_iap_state_names[iap->state], err_str ? err_str : "no error");

  icd_idle_timer_unset(iap);

  switch ( iap->state )
  {
    case ICD_IAP_STATE_DISCONNECTED:
      ILOG_WARN("disconnect requested for already disconnected IAP %p", iap);
      break;
    case ICD_IAP_STATE_SCRIPT_PRE_UP:
      ILOG_INFO("disconnect requested for IAP %p in pre_up", iap);
      iap->err_str = g_strdup(err_str);
      icd_iap_disconnect_module(iap);
      break;
    case ICD_IAP_STATE_LINK_UP:
    {
      struct icd_network_module *module = NULL;

      iap->err_str = g_strdup(err_str);

      if (iap->current_module)
      {
        module = (struct icd_network_module *)iap->current_module->data;

        if (module && module->nw.link_down )
        {
          ILOG_INFO("calling link_down function in last module '%s' when disconnecting",
                    module->name);

          iap->state = ICD_IAP_STATE_LINK_DOWN;
          module->nw.link_down(iap->connection.network_type,
                               iap->connection.network_attrs,
                               iap->connection.network_id, NULL,
                               icd_iap_disconnect_cb, iap, &module->nw.private);
          return;
        }
      }

      ILOG_INFO("no link_down function in last tried module '%s' when disconnecting",
                module ? module->name : NULL);

      icd_iap_disconnect_module(iap);
      break;
    }
    case ICD_IAP_STATE_LINK_POST_UP:
    {
      struct icd_network_module *module = NULL;

      iap->err_str = g_strdup(err_str);

      if (iap->current_module)
      {
        module = (struct icd_network_module *)iap->current_module->data;

        if (module && module->nw.link_pre_down)
        {
          ILOG_INFO("calling link_pre_down function in last module '%s' when disconnecting",
                    module->name);

          iap->state = ICD_IAP_STATE_LINK_PRE_DOWN;
          module->nw.link_pre_down(iap->connection.network_type,
                                   iap->connection.network_attrs,
                                   iap->connection.network_id,
                                   iap->interface_name,
                                   icd_iap_disconnect_cb, iap,
                                   &module->nw.private);
          return;
        }
      }

      ILOG_INFO("no link_pre_down function in last tried module '%s' when disconnecting",
                module ? module->name : NULL);

      icd_iap_disconnect_module(iap);
      break;
    }
    case ICD_IAP_STATE_IP_UP:
    {
      struct icd_network_module *module = NULL;

      iap->err_str = g_strdup(err_str);

      if (iap->current_module)
      {
        module = (struct icd_network_module *)iap->current_module->data;

        if (module && module->nw.ip_down)
        {
          ILOG_INFO("calling ip_down function in last tried module '%s' when disconnecting",
                    module->name);

          iap->state = ICD_IAP_STATE_IP_DOWN;
          module->nw.ip_down(
                iap->connection.network_type,
                iap->connection.network_attrs,
                iap->connection.network_id,
                iap->interface_name,
                icd_iap_disconnect_cb, iap,
                &module->nw.private);
          return;
        }
      }

      ILOG_INFO("no ip_down function in last tried module '%s' when disconnecting",
                module ? module->name : NULL);

      icd_iap_disconnect_module(iap);
      break;
    }
    case ICD_IAP_STATE_SRV_UP:
      iap->err_str = g_strdup(err_str);

      if (iap->limited_conn)
      {
        iap->limited_conn = FALSE;
        icd_status_limited_conn(iap, NULL, NULL);
      }

      if (icd_srv_provider_disconnect(iap, icd_iap_srv_disconnect_cb, iap))
        ILOG_INFO("called srv disconnect function when disconnecting");
      else
      {
        ILOG_INFO("no srv module to call when disconnecting");
        icd_iap_disconnect_module(iap);
      }
      break;
    case ICD_IAP_STATE_SAVING:
      icd_osso_ui_send_save_cancel(iap->save_dlg);
    case ICD_IAP_STATE_SCRIPT_POST_UP:
    case ICD_IAP_STATE_CONNECTED:
      ILOG_INFO("disconnect requested for IAP %p", iap);
      iap->err_str = g_strdup(err_str);
      iap->state = ICD_IAP_STATE_CONNECTED_DOWN;

      icd_iap_script_pre_down(iap);
      break;
    default:
      ILOG_INFO("disconnect requested for already disconnecting IAP %p", iap);
      break;
  }
}

/**
 * Find an IAP according type, attributes and id
 *
 * @param network_type   the type of the IAP
 * @param network_attrs  attributes
 * @param network_id     IAP id
 *
 * @return  a pointer to the IAP on success, NULL on failure
 */
struct icd_iap *
icd_iap_find(const gchar *network_type, const guint network_attrs,
             const gchar *network_id)
{
  GSList *l;

  for (l = icd_context_get()->request_list; l; l = l->next)
  {
    struct icd_request *request = (struct icd_request *)l->data;

    if (request)
    {
      if (request->try_iaps)
      {
        struct icd_iap *iap = (struct icd_iap *)request->try_iaps->data;

        if (((network_attrs & ICD_NW_ATTR_LOCALMASK) ==
             (iap->connection.network_attrs & ICD_NW_ATTR_LOCALMASK) ||
             (iap->connection.network_attrs & ICD_NW_ATTR_IAPNAME) ==
             (network_attrs & ICD_NW_ATTR_IAPNAME)) &&
            string_equal(network_type, iap->connection.network_type) &&
            string_equal(network_id, iap->connection.network_id))
        {
          ILOG_DEBUG("IAP for %s/%0x/%s found", network_type, network_attrs,
                     network_id);
          return iap;
        }
      }
      else
        ILOG_ERR("request %p does not have iaps", request);
    }
    else
      ILOG_ERR("request in request list is NULL");
  }

  return NULL;
}

/**
 * Report the final status of the connection attempt to the caller. Do notice
 * that the IAP is freed by the caller, do not use it after calling this
 * function
 *
 * @param status  the status to report
 * @param iap     the IAP
 */
static void
icd_iap_do_callback(enum icd_iap_status status, struct icd_iap *iap)
{
  ILOG_INFO("IAP status is %s", icd_iap_status_names[status]);

  iap->request_cb(status, iap, iap->request_cb_user_data);
}

/**
 * Reset the list of modules to try
 * @param iap  the IAP
 */
static void
icd_iap_modules_reset(struct icd_iap *iap)
{
  iap->current_module = NULL;
}

/**
 * Notify the caller (request) that the IAP has connected
 * @param iap  the IAP
 */
static void
icd_iap_has_connected(struct icd_iap *iap)
{
  iap->state = ICD_IAP_STATE_CONNECTED;
  icd_iap_modules_reset(iap);
  icd_idle_timer_set(iap);
  icd_iap_do_callback(ICD_IAP_CREATED, iap);
}

/**
 * Rename an IAP and continue connecting it if it's in #ICD_IAP_STATE_SAVING
 * @param iap   the IAP
 * @param name  the new name of the IAP
 */
gboolean
icd_iap_rename(struct icd_iap *iap, const gchar *name)
{
  gboolean rv = FALSE;

  if (iap->id && !iap->id_is_local)
  {
    rv = icd_gconf_rename(iap->id, name);

    ILOG_INFO("IAP %p settings '%s' renamed to '%s'", iap, iap->id, name);
  }
  else
    ILOG_ERR("iap id is unset when renaming");

  if (iap->state == ICD_IAP_STATE_SAVING)
    icd_iap_has_connected(iap);

  return rv;
}

/**
 * Free up an iap structure
 * @param iap  the IAP to free
 */
void
icd_iap_free(struct icd_iap *iap)
{
  GSList *l1;
  GSList *l2;

  if (!iap)
    return;

  if (iap->current_module || iap->ip_down_list || iap->link_pre_down_list ||
      iap->link_down_list)
  {
    ILOG_CRIT("Removing active IAP %p/%p/%p/%p", iap->current_module,
              iap->ip_down_list, iap->link_pre_down_list, iap->link_down_list);
  }

  if ( iap->id && !iap->id_is_local )
  {
    if (*iap->id)
      icd_gconf_remove_temporary(iap->id);

    ILOG_DEBUG("IAP %s/%s/0x%04x cache check", iap->connection.network_id,
               iap->connection.network_type, iap->connection.network_attrs);

    if (iap->current_module)
    {
      struct icd_network_module *module =
          (struct icd_network_module *)g_slist_nth_data(iap->current_module, 0);
      struct icd_scan_cache_list *cache_list =
          icd_scan_cache_list_lookup(module, iap->connection.network_id);

      if (cache_list)
      {
        ILOG_DEBUG("IAP %s found in the cache list, removing it.",
                   iap->connection.network_id);

        if (icd_scan_cache_entry_remove(cache_list, iap->connection.network_id,
                                        iap->connection.network_type,
                                        iap->connection.network_attrs))
        {
          ILOG_DEBUG("Removed temp IAP %p from cache.", iap);
        }
        else
          ILOG_DEBUG("Temp IAP %p not found in cache.", iap);
      }
    }
  }

  g_free(iap->id);
  g_free(iap->connection.service_type);
  g_free(iap->service_name);
  g_free(iap->connection.service_id);
  g_free(iap->connection.network_type);
  g_free(iap->network_name);
  g_free(iap->connection.network_id);
  g_free(iap->interface_name);
  g_free(iap->err_str);

  for (l1 = iap->script_env; l1; iap->script_env = l1)
  {
    struct icd_iap_env *env = l1->data;

    for (l2 = env->envlist; l2; env->envlist = l2)
    {
      g_free(l2->data);
      l2 = g_slist_delete_link(env->envlist, env->envlist);
    }

    g_free(env->addrfam);
    g_free(env);

    l1 = g_slist_delete_link(iap->script_env, iap->script_env);
  }

  ILOG_DEBUG("Freeing IAP %p", iap);

  g_free(iap);
}

/**
 * Renew function callback
 * @param status       renewal status
 * @param renew_token  the IAP that is being renewed
 */
static void
icd_iap_run_renew_cb(enum icd_nw_renew_status status, gpointer renew_token)
{
  struct icd_iap *iap = (struct icd_iap *)renew_token;

  if (!iap )
  {
    ILOG_ERR("NULL renew token received");
    return;
  }

  if (status == ICD_NW_RENEW_CHANGES_MADE)
  {
    enum icd_nw_layer renew_layer = iap->renew_layer;

    iap->current_renew_module = NULL;
    iap->renew_layer = ICD_NW_LAYER_NONE;

    ILOG_DEBUG("renew returned %d for iap %p, restarting layer %s", status,
               iap, icd_iap_layer_names[renew_layer]);
    icd_iap_restart(iap, renew_layer);
  }
  else
  {
    if (iap->current_renew_module)
      iap->current_renew_module = iap->current_renew_module->next;

    if (!icd_iap_run_renew(iap))
    {
      ILOG_DEBUG("nothing more to renew for iap %p, %s/%0x/%s", iap,
                 iap->connection.network_type, iap->connection.network_attrs,
                 iap->connection.network_id);
      iap->renew_layer = ICD_NW_LAYER_NONE;
    }
  }
}

/**
 * Run the renew function for the specified IAP
 *
 * @param iap  the IAP
 *
 * @return     TRUE if a renew network module function is called; FALSE if no
 *             further renew functions can be found
 */
static gboolean
icd_iap_run_renew(struct icd_iap *iap)
{
  GSList *current_module;
  enum icd_nw_layer renew_layer;

  current_module = iap->current_renew_module;

  if (!current_module)
  {
    ILOG_DEBUG("no more nw modules to renew for iap %p", iap);
    return FALSE;
  }

  renew_layer = iap->renew_layer;

  if (renew_layer == ICD_NW_LAYER_IP)
  {
    while (current_module)
    {
      struct icd_network_module *module =
          (struct icd_network_module *)current_module->data;

          if (module)
          {
            if (module->nw.ip_renew)
            {
              ILOG_DEBUG("renew ip layer for module '%s'", module->name);
              module->nw.ip_renew(iap->connection.network_type,
                                  iap->connection.network_attrs,
                                  iap->connection.network_id,
                                  icd_iap_run_renew_cb, iap,
                                  &module->nw.private);

              return !!iap->current_renew_module;
            }

            ILOG_DEBUG("module '%s' does not have an ip layer renew function",
                       module->name);
            if (iap->current_renew_module)
              iap->current_renew_module = iap->current_renew_module->next;
          }
          else
            iap->current_renew_module = current_module->next;

          current_module = iap->current_renew_module;
    }
  }
  else if (renew_layer == ICD_NW_LAYER_LINK)
  {
    while (current_module)
    {
      struct icd_network_module *module =
          (struct icd_network_module *)current_module->data;

          if (module)
          {
            if (module->nw.link_renew)
            {
              ILOG_DEBUG("renew link layer for module '%s'", module->name);
              module->nw.link_renew(iap->connection.network_type,
                                    iap->connection.network_attrs,
                                    iap->connection.network_id,
                                    icd_iap_run_renew_cb, iap,
                                    &module->nw.private);

              return !!iap->current_renew_module;
            }

            ILOG_DEBUG("module '%s' does not have a link layer renew function",
                       module->name);

            if (iap->current_renew_module)
              iap->current_renew_module = iap->current_renew_module->next;
          }
          else
            iap->current_renew_module = current_module->next;

          current_module = iap->current_renew_module;
    }
  }
  else if(renew_layer == ICD_NW_LAYER_LINK_POST)
  {
    while (current_module)
    {
      struct icd_network_module *module =
          (struct icd_network_module *)current_module->data;

          if (module)
          {
            if (!module->nw.link_post_renew)
            {
              ILOG_DEBUG("module '%s' does not have a link post layer renew function",
                         module->name);

              if (iap->current_renew_module)
                iap->current_renew_module = iap->current_renew_module->next;
            }

            ILOG_DEBUG("renew link post layer for module '%s'", module->name);
            module->nw.link_post_renew(iap->connection.network_type,
                                       iap->connection.network_attrs,
                                       iap->connection.network_id,
                                       icd_iap_run_renew_cb, iap,
                                       &module->nw.private);

            return !!iap->current_renew_module;
          }
          else
            iap->current_renew_module = current_module->next;

          current_module = iap->current_renew_module;
    }
  }
  else
  {
    ILOG_DEBUG("renew for %s not supported",
               icd_iap_layer_names[iap->renew_layer]);
  }

  return FALSE;
}

/**
 * Request a renew for the specified IAP and network layer
 * @param iap          the IAP
 * @param renew_layer  the network module layer to renew
 */
void
icd_iap_renew(struct icd_iap *iap, enum icd_nw_layer renew_layer)
{
  if (iap->renew_layer)
  {
    ILOG_DEBUG("ignoring iap %p renew, already renewing %s", iap,
               icd_iap_layer_names[iap->renew_layer]);
    return;
  }

  iap->renew_layer = renew_layer;
  iap->current_renew_module = iap->network_modules;

  if (!icd_iap_run_renew(iap))
  {
    ILOG_DEBUG("no renew function for %s iap %p, %s/%0x/%s, restarting %s",
               icd_iap_layer_names[renew_layer], iap,
               iap->connection.network_type, iap->connection.network_attrs,
               iap->connection.network_id, icd_iap_layer_names[renew_layer]);

    iap->renew_layer = ICD_NW_LAYER_NONE;
    icd_iap_restart(iap, renew_layer);
  }
}

/**
 * Callback for _up functions; adds _down functions and calls the next module
 * on success, starts disconnecting on failure
 *
 * @param status    status
 * @param err_str   error string or NULL if no error
 * @param cb_token  the IAP
 */
static void
icd_iap_up_callback(const enum icd_nw_status status, const gchar *err_str,
                    const gpointer cb_token)
{
  struct icd_iap *iap = (struct icd_iap *)cb_token;
  struct icd_network_module *module;

  if (!iap)
    ILOG_CRIT("_up callback returns NULL iap");

  ILOG_INFO("iap %p callback in state %s with status %d, error '%s', interface '%s'",
            iap, icd_iap_state_names[iap->state], status, err_str,
            iap->interface_name);

  if (iap->current_module)
    module = (struct icd_network_module *)iap->current_module->data;
  else
  {
    if (iap->state == ICD_IAP_STATE_LINK_UP ||
        iap->state == ICD_IAP_STATE_LINK_POST_UP ||
        iap->state == ICD_IAP_STATE_IP_UP)
    {
      ILOG_CRIT("_up callback current module is NULL in state %s",
                icd_iap_state_names[iap->state]);
      return;
    }

    module = NULL;
  }

  switch (status)
  {
    case ICD_NW_SUCCESS:
    case ICD_NW_SUCCESS_NEXT_LAYER:
    {
      switch (iap->state)
      {
        case ICD_IAP_STATE_LINK_UP:
        {
          ILOG_INFO("module '%s' link_up callback", module->name);

          if (module->nw.link_down)
          {
            struct icd_iap_disconnect_data *data =
                g_new0(struct icd_iap_disconnect_data, 1);

            data->function = module->nw.link_down;
            data->private = &module->nw.private;
            iap->link_down_list = g_slist_prepend(iap->link_down_list, data);

            ILOG_DEBUG("Added link_down %p from '%s' to iap",
                       module->nw.link_down, module->name);
          }
          else
          {
            ILOG_INFO("Module '%s' does not have a link_down function",
                      module->name);
          }

          break;
        }
        case ICD_IAP_STATE_LINK_POST_UP:
        {
          ILOG_INFO("module '%s' link_post_up callback", module->name);

          if (module->nw.link_pre_down)
          {
            struct icd_iap_disconnect_data *data =
                g_new0(struct icd_iap_disconnect_data, 1);

            data->function = module->nw.link_pre_down;
            data->private = &module->nw.private;
            iap->link_pre_down_list =
                g_slist_prepend(iap->link_pre_down_list, data);
            ILOG_DEBUG("Added link_pre_down %p from '%s' to iap",
                       module->nw.link_pre_down, module->name);
          }
          else
          {
            ILOG_DEBUG("Module '%s' does not have a link_pre_down function",
                       module->name);
          }

          break;
        }
        case ICD_IAP_STATE_IP_UP:
        {
          ILOG_INFO("module '%s' ip_up callback", module->name);

          if (module->nw.ip_down)
          {
            struct icd_iap_disconnect_data *data =
                g_new0(struct icd_iap_disconnect_data, 1);

            data->function = module->nw.ip_down;
            data->private = &module->nw.private;
            iap->ip_down_list = g_slist_prepend(iap->ip_down_list, data);

            ILOG_DEBUG("Added ip_down %p from '%s' to iap", module->nw.ip_down,
                       module->name);
          }
          else
          {
            ILOG_DEBUG("Module '%s' does not have an ip_down function",
                       module->name);
          }

          break;
        }
        case ICD_IAP_STATE_SRV_UP:
        {
          ILOG_INFO("service module connect callback");
          break;
        }
        default:
        {
          ILOG_ERR("State %d does not add any _down functions", iap->state);
          break;
        }
      }

      if (status == ICD_NW_SUCCESS_NEXT_LAYER)
      {
        ILOG_INFO("'%s' requests ICd to start with next level",
                  module ? module->name : "srv provider");

        if (iap->state == ICD_IAP_STATE_LINK_UP ||
            iap->state == ICD_IAP_STATE_LINK_POST_UP)
        {
          icd_iap_modules_reset(iap);
          iap->state++;
        }
      }
      else
        ILOG_INFO("checking for same layer _up functions in other modules");

      icd_iap_module_next(iap);
      break;
    }
    case ICD_NW_RESTART:
    {
      icd_iap_restart(iap, ICD_NW_LAYER_ALL);
      break;
    }
    case ICD_NW_TOO_MANY_CONNECTIONS:
    {
      ILOG_INFO("iap %p ICD_NW_TOO_MANY_CONNECTIONS reported by '%s', %p",
                iap, module ? module->name : "srv provider", module);
      iap->busy = module;
      icd_iap_disconnect_module(iap);
      break;
    }
    case ICD_NW_ERROR_USER_ACTION_DONE:
    {
      ILOG_INFO("ICD_NW_ERROR_USER_ACTION_DONE requested for iap %p by '%s' with error '%s'",
                iap, module ? module->name : "srv provider", err_str);

      iap->user_interaction_done = TRUE;
      g_free(iap->err_str);
      iap->err_str = g_strdup(err_str);
      icd_iap_disconnect_module(iap);
      break;
    }
    case ICD_NW_RESTART_IP:
    {
      icd_iap_restart(iap, ICD_NW_LAYER_IP);
      break;
    }
    case ICD_NW_RESTART_LINK_POST:
    {
      icd_iap_restart(iap, ICD_NW_LAYER_LINK_POST);
      break;
    }
    case ICD_NW_RESTART_LINK:
    {
      icd_iap_restart(iap, ICD_NW_LAYER_LINK);
      break;
    }
    default:
    {
      if (iap->err_str)
        ILOG_INFO("IAP already has error set, ignoring given '%s'", err_str);
      else if (err_str)
      {
        iap->err_str = g_strdup(err_str);
        ILOG_INFO("IAP reports error '%s'", err_str);
      }
      else
      {
        iap->err_str = g_strdup(ICD_DBUS_ERROR_NETWORK_ERROR);
        ILOG_INFO("IAP reports error, but error string NULL, set to '%s'",
                  iap->err_str);
      }

      icd_iap_disconnect_module(iap);
      break;
    }
  }
}

/**
 * Callback for link_post_up; common _up callback handling in
 * icd_iap_up_callback()
 *
 * @param status            status of the operation
 * @param err_str           NULL if the network was disconnected normally or
 *                          any ICD_DBUS_ERROR_* from osso-ic-dbus.h on error
 * @param link_up_cb_token  the IAP in question
 * @param ...               zero or more arrays of strings where each string
 *                          in the array is an environment variable of the
 *                          form name=value; end with NULL
 */
static void
icd_iap_link_post_up_cb(const enum icd_nw_status status, const gchar *err_str,
                        gpointer link_post_up_cb_token, ...)
{
  struct icd_iap *iap = (struct icd_iap *)link_post_up_cb_token;
  gchar **env_vars;
  va_list ap;

  va_start(ap, link_post_up_cb_token);
  env_vars = va_arg(ap, gchar **);

  if (iap)
  {
    if (iap->state == ICD_IAP_STATE_LINK_POST_UP)
    {
      if (status == ICD_NW_SUCCESS_NEXT_LAYER || status == ICD_NW_SUCCESS)
      {
        ILOG_DEBUG("starting with new script env set %p", env_vars);

        while (env_vars)
        {
          icd_script_add_env_vars(iap, env_vars);
          ILOG_DEBUG("env set %p handled", env_vars);
          env_vars = va_arg(ap, gchar **);
        }
      }

      icd_iap_up_callback(status, err_str, iap);
    }
    else
    {
      ILOG_CRIT("iap is in state %s when calling link_post_up",
                icd_iap_state_names[iap->state]);
    }
  }
  else
    ILOG_CRIT("link_post_up callback returns NULL iap");

  va_end(ap);
}

/**
 * Service provider connect callback function
 * @param status     status of the connect
 * @param user_data  user data
 */
static void
icd_iap_srv_connect_cb(enum icd_srv_status status, const gchar *err_str,
                       gpointer user_data)
{
  struct icd_iap *iap = (struct icd_iap *)user_data;

  if (iap)
  {
    if (iap->state == ICD_IAP_STATE_SRV_UP )
    {
      enum icd_nw_status nw_status = ICD_NW_SUCCESS;

      if (status == ICD_SRV_RESTART)
          nw_status = ICD_NW_RESTART;
      else if (status == ICD_SRV_ERROR)
          nw_status = ICD_NW_ERROR;

      icd_iap_up_callback(nw_status, err_str, user_data);
    }
    else
    {
      ILOG_CRIT("iap is in state %s when calling srv_connect_cb",
                icd_iap_state_names[iap->state]);
    }
  }
  else
    ILOG_CRIT("srv_connect callback returns NULL iap");
}

/**
 * Callback for link_up; saves the interface name on success, common _up
 * callback handling in icd_iap_up_callback()
 *
 * @param status            status of the operation
 * @param err_str           NULL if the network was disconnected normally or
 *                          any ICD_DBUS_ERROR_* from osso-ic-dbus.h on error
 * @param interface_name    the device interface name on ICD_NW_SUCCESS*
 * @param link_up_cb_token  the IAP in question
 * @param ...               zero or more arrays of strings where each string
 *                          in the array is an environment variable of the
 *                          form name=value; end with NULL
 */
static void
icd_iap_link_up_cb(const enum icd_nw_status status, const gchar *err_str,
                   const gchar *interface_name, gpointer link_up_cb_token, ...)
{
  struct icd_iap *iap = (struct icd_iap *)link_up_cb_token;
  gchar **env_vars;
  va_list ap;

  va_start(ap, link_up_cb_token);
  env_vars = va_arg(ap, gchar **);

  if (iap)
  {
    if (iap->state == ICD_IAP_STATE_LINK_UP)
    {
      if (status == ICD_NW_SUCCESS || status == ICD_NW_SUCCESS_NEXT_LAYER)
      {
        ILOG_DEBUG("starting with new script env set %p", env_vars);

        while (env_vars)
        {
          icd_script_add_env_vars(iap, env_vars);
          ILOG_DEBUG("env set %p handled", env_vars);
          env_vars = va_arg(ap, gchar **);
        }

        if (interface_name)
        {
          g_free(iap->interface_name);
          iap->interface_name = g_strdup(interface_name);
        }

        ILOG_DEBUG("iap %p link_up callback got '%s', set to '%s'", iap,
                   interface_name, iap->interface_name);
      }
      else
      {
        ILOG_DEBUG("iap %p link_up callback did not set interface '%s'", iap,
                   interface_name);
      }

      icd_iap_up_callback(status, err_str, iap);
    }
    else if (iap->state > ICD_IAP_STATE_SCRIPT_POST_DOWN)
      ILOG_CRIT("iap is in invalid state %d when calling link_up", iap->state);
    else
    {
      ILOG_CRIT("iap is in state %s when calling link_up",
                icd_iap_state_names[iap->state]);
    }
  }
  else
    ILOG_CRIT("link_up callback returns NULL iap");

  va_end(ap);
}

/**
 * Callback function called when IP address configuration has completed
 *
 * @param status          status of the operation
 * @param err_str         NULL if the network was disconnected normally or an
 *                        error string
 * @param ip_up_cb_token  the callback token
 * @param ...             zero or more arrays of strings where each string in
 *                        the array is an environment variable of the form
 *                        name=value; end with NULL
 */
static void icd_iap_ip_up_cb(const enum icd_nw_status status,
                             const gchar *err_str, gpointer ip_up_cb_token, ...)
{
  struct icd_iap *iap = (struct icd_iap *)ip_up_cb_token;
  gchar **env_vars;
  va_list ap;

  va_start(ap, ip_up_cb_token);
  env_vars = va_arg(ap, gchar **);

  if (iap)
  {
    if (iap->state == ICD_IAP_STATE_IP_UP)
    {
      if (status  == ICD_NW_SUCCESS || status == ICD_NW_SUCCESS_NEXT_LAYER)
      {
        ILOG_DEBUG("starting with new script env set %p", env_vars);

        while (env_vars)
        {
          icd_script_add_env_vars(iap, env_vars);
          ILOG_DEBUG("env set %p handled", env_vars);
          env_vars = va_arg(ap, gchar **);
        }
      }

      icd_iap_up_callback(status, err_str, iap);
    }
    else
    {
      ILOG_CRIT("iap is in state %s when calling ip_up",
                icd_iap_state_names[iap->state]);
    }
  }
  else
    ILOG_CRIT("ip_up callback returns NULL iap");

  va_end(ap);
}

/**
 * Find the next module that has implemented xxx()
 *
 * @param iap     the IAP
 * @param offset  method offset in #icd_nw_api
 *
 * @return        the next module or NULL if none
 */
static struct icd_network_module*
icd_iap_next_xxx_module(struct icd_iap *iap, glong offset)
{
  if (iap->current_module)
    iap->current_module = iap->current_module->next;
  else
    iap->current_module = iap->network_modules;

  while (iap->current_module)
  {
    struct icd_network_module *module =
        (struct icd_network_module *)iap->current_module->data;

    if (module && G_STRUCT_MEMBER(gpointer, &module->nw, offset))
      return module;

    iap->current_module = iap->current_module->next;
  }

  return NULL;
}

/**
 * Continue (or start) connecting an IAP by finding a suitable _up function
 * from the available modules. Calls icd_iap_connect_module() to call the
 * relevant _up function
 *
 * @param iap  the IAP to connect
 */
static void
icd_iap_module_next(struct icd_iap *iap)
{
  ILOG_WARN("connecting iap %p in state %s: interface is '%s'", iap,
            icd_iap_state_names[iap->state],
            iap->interface_name);

  switch (iap->state)
  {
    case ICD_IAP_STATE_SCRIPT_PRE_UP:
    case ICD_IAP_STATE_LINK_UP:
    {
      struct icd_network_module *module;

      iap->state = ICD_IAP_STATE_LINK_UP;
      module = icd_iap_next_xxx_module(iap,
          G_STRUCT_OFFSET(struct icd_nw_api, link_up));

      if (module)
      {
        ILOG_INFO("calling module '%s' link_up", module->name);
        module->nw.link_up(iap->connection.network_type,
                           iap->connection.network_attrs,
                           iap->connection.network_id, icd_iap_link_up_cb,
                           iap, &module->nw.private);
        return;
      }

      ILOG_DEBUG("No more link_up functions found for network type '%s'",
                 iap->connection.network_type);
    }
    case ICD_IAP_STATE_LINK_POST_UP:
    {
      struct icd_network_module *module;

      iap->state = ICD_IAP_STATE_LINK_POST_UP;
      module = icd_iap_next_xxx_module(iap,
          G_STRUCT_OFFSET(struct icd_nw_api, link_post_up));

      if (module)
      {
        ILOG_DEBUG("calling module '%s' link_post_up", module->name);

        module->nw.link_post_up(iap->connection.network_type,
                                iap->connection.network_attrs,
                                iap->connection.network_id,
                                iap->interface_name, icd_iap_link_post_up_cb,
                                iap, &module->nw.private);
        return;
      }

      ILOG_DEBUG("No other link_post_up functions found");
    }
    case ICD_IAP_STATE_IP_UP:
    {
      struct icd_network_module *module;

      iap->state = ICD_IAP_STATE_IP_UP;
      module = icd_iap_next_xxx_module(iap,
          G_STRUCT_OFFSET(struct icd_nw_api, ip_up));

      if (module)
      {
        ILOG_INFO("calling module '%s' ip_up", module->name);
        module->nw.ip_up(iap->connection.network_type,
                         iap->connection.network_attrs,
                         iap->connection.network_id,
                         iap->interface_name, icd_iap_ip_up_cb,
                         iap, &module->nw.private);
        return;
      }

      ILOG_DEBUG("No other ip_up functions found");
      icd_dbus_api_update_state(iap, 0, ICD_STATE_INTERNAL_ADDRESS_ACQUIRED);
    }
    case ICD_IAP_STATE_SRV_UP:
    {
      iap->state = ICD_IAP_STATE_SRV_UP;

      if (icd_srv_provider_has_next(iap))
      {

        ILOG_DEBUG("calling service provider module");

        if (icd_srv_provider_connect(iap, icd_iap_srv_connect_cb, iap))
          return;
      }
      else
        ILOG_DEBUG("No service provider module to call");

      if (iap->limited_conn)
      {
        iap->limited_conn = FALSE;
        icd_status_limited_conn(iap, NULL, NULL);
      }
    }
    case ICD_IAP_STATE_SCRIPT_POST_UP:
    {
      gchar *iap_id;
      GSList *scr_env;
      pid_t pid;
      gboolean post_up_called = FALSE;

      iap->state = ICD_IAP_STATE_SCRIPT_POST_UP;

      if (iap->id && !iap->id_is_local)
        iap_id = gconf_escape_key(iap->id, -1);
      else
        iap_id = NULL;

      for (scr_env = iap->script_env; scr_env; scr_env = scr_env->next)
      {
        struct icd_iap_env *env = (struct icd_iap_env *)scr_env->data;

        if (env)
        {
          pid = icd_script_post_up(iap->interface_name, iap_id,
                                   iap->connection.network_type, env,
                                   icd_iap_post_up_script_done, iap);
          post_up_called = TRUE;
          iap->script_pids = g_slist_prepend(iap->script_pids,
                                             GINT_TO_POINTER(pid));
        }
      }

      if (!post_up_called)
      {
        ILOG_INFO("no env vars for post-up script");
        pid = icd_script_post_up(iap->interface_name, iap_id,
                                 iap->connection.network_type, NULL,
                                 icd_iap_post_up_script_done, iap);

        iap->script_pids = g_slist_prepend(iap->script_pids,
                                           GINT_TO_POINTER(pid));
      }

      g_free(iap_id);
      break;
    }
    default:
    {
      ILOG_ERR("IAP cannot be in state %s on connect",
               icd_iap_state_names[iap->state]);
    }
  }
}

/**
 * Callback function called when pre-up scripts have been run
 *
 * @param pid         the process id of the script that exited
 * @param exit_value  exit value of the script or -1 on timeout
 * @param user_data   user data
 */
static void
icd_iap_pre_up_script_done(const pid_t pid, const gint exit_value,
                           gpointer user_data)
{
  struct icd_iap *iap = (struct icd_iap *)user_data;

  iap->script_pids = g_slist_remove(iap->script_pids, GINT_TO_POINTER(pid));

  ILOG_DEBUG("iap %p in state %s pre-up scripts run, continue connecting", iap,
             icd_iap_state_names[iap->state]);

  switch (iap->state)
  {
    case ICD_IAP_STATE_LINK_PRE_RESTART_SCRIPTS:
      iap->state = ICD_IAP_STATE_LINK_POST_UP;
      break;
    case ICD_IAP_STATE_LINK_RESTART_SCRIPTS:
      iap->state = ICD_IAP_STATE_LINK_UP;
      break;
    case ICD_IAP_STATE_IP_RESTART_SCRIPTS:
      iap->state = ICD_IAP_STATE_IP_UP;
      break;
    default:
      /* Shut up the compiler */
      break;
  }

  icd_iap_module_next(iap);
}

/**
 * Run pre up scripts
 * @param iap  the IAP
 */
static void
icd_iap_run_pre_up_scripts(struct icd_iap *iap)
{
  char *id;
  pid_t pid;

  if (iap->id && !iap->id_is_local )
    id = gconf_escape_key(iap->id, -1);
  else
    id = NULL;

  pid = icd_script_pre_up(id, iap->connection.network_type, NULL,
                          icd_iap_pre_up_script_done, iap);
  g_free(id);
  iap->script_pids = g_slist_prepend(iap->script_pids, GINT_TO_POINTER(pid));
}

/**
 * Request a network connection. The caller needs to free the given icd_iap
 * data structure when the IAP is no longer in use.
 *
 * @param iap         IAP to connect
 * @param request_cb  the callback to call when the outcome of the request is
 *                    known
 * @param user_data   user data to pass to the callback
 */
void
icd_iap_connect(struct icd_iap *iap, icd_iap_request_cb_fn request_cb,
                gpointer user_data)
{
  struct icd_context *icd_ctx = icd_context_get();
  GSList *modules;

  if (!request_cb)
  {
    ILOG_CRIT("IAP connect callback is NULL");
    return;
  }

  if (!iap)
  {
      ILOG_CRIT("IAP to try is NULL");
      request_cb(ICD_IAP_FAILED, 0, user_data);
      return;
  }

  iap->busy = FALSE;

  if (iap->state != ICD_IAP_STATE_DISCONNECTED )
  {
    ILOG_ERR("IAP %p is in state %s, not connecting it again", iap,
             icd_iap_state_names[iap->state]);
    return;
  }

  iap->request_cb = request_cb;
  iap->request_cb_user_data = user_data;
  modules = (GSList *)g_hash_table_lookup(icd_ctx->type_to_module,
                                          iap->connection.network_type);
  iap->network_modules = modules;

  if (modules)
  {
    icd_iap_modules_reset(iap);

    ILOG_DEBUG("Request to connect iap %p", iap);

    iap->state = ICD_IAP_STATE_SCRIPT_PRE_UP;
    icd_iap_run_pre_up_scripts(iap);
  }
  else
  {
    ILOG_ERR("unknown network type '%s' requested for iap %p",
             iap->connection.network_type, iap);
    icd_iap_do_callback(ICD_IAP_FAILED, iap);
  }
}

/**
 * Allocate memory for a new IAP structure. Caller is responsible of freeing
 * the IAP structure with icd_iap_free() after use
 *
 * @return  the newly created IAP structure
 */
struct icd_iap *
    icd_iap_new(void)
{
  return g_new0(struct icd_iap, 1);
}

/**
 * Find an IAP according to id and locally generated flag
 *
 * @param iap_id    IAP id
 * @param is_local  TRUE if a locally generated icd2 id is requested, FALSE
 *                  otherwise
 *
 * @return  a pointer to the IAP on success, NULL on failure
 */
struct icd_iap *
icd_iap_find_by_id(const gchar *iap_id, const gboolean is_local)
{
  GSList *l;

  for (l = icd_context_get()->request_list; l; l = l->next)
  {
    struct icd_request *request = (struct icd_request *)l->data;

    if (request)
    {
      if (request->try_iaps)
      {
        if (request->try_iaps->data)
        {
            struct icd_iap *iap = (struct icd_iap *)request->try_iaps->data;

          if (string_equal(iap_id, iap->id) && iap->id_is_local == is_local)
          {
            ILOG_DEBUG("IAP for %s and local %s found", iap_id,
                       is_local ? "TRUE" : "FALSE");
            return iap;
          }
        }
        else
          ILOG_ERR("request %p contains NULL iap", request);
      }
      else
        ILOG_DEBUG("request %p does not have iaps", request);
    }
    else
      ILOG_ERR("request in request list is NULL");
  }

  return NULL;
}

/**
 * Callback for save connection dialog request
 * @param success    TRUE on success, FALSE on failure
 * @param user_data  the IAP
 */
static void
icd_iap_save_cb(gboolean success, gpointer user_data)
{
  struct icd_iap *iap = (struct icd_iap *)user_data;

  iap->save_dlg = NULL;

  if (success)
    ILOG_DEBUG("save connection dialog successfully requested, waiting...");
  else
  {
    ILOG_WARN("save connection dialog not ok, continue without saving");
    icd_iap_has_connected(iap);
  }
}

/**
 * A post-up script has exited
 *
 * @param pid         the process id of the script that exited
 * @param exit_value  exit value of the script or -1 on timeout
 * @param user_data   the current IAP
 *
 * @todo  what to do with this iap if UI goes down?
 */
static void
icd_iap_post_up_script_done(const pid_t pid, const gint exit_value,
                            gpointer user_data)
{
  struct icd_iap *iap = (struct icd_iap *)user_data;

  iap->script_pids = g_slist_remove(iap->script_pids, GINT_TO_POINTER(pid));

  if (iap->script_pids)
    ILOG_INFO("still more post-up scripts to come, waiting");
  else
  {
    ILOG_INFO("all post-up scripts done, iap can be connected");

    if (!iap->id_is_local && icd_gconf_is_temporary(iap->id))
    {
      iap->state = ICD_IAP_STATE_SAVING;
      iap->save_dlg = icd_osso_ui_send_save(iap->connection.network_id,
                                            icd_iap_save_cb, iap);
    }
    else
      icd_iap_has_connected(iap);
  }
}

/**
 * Get IP address info from an IAP
 *
 * @param iap        IAP
 * @param cb         callback function
 * @param user_data  user data
 *
 * @return  the number of times the callback is going to be called
 */
guint
icd_iap_get_ipinfo(struct icd_iap *iap, icd_nw_ip_addr_info_cb_fn cb,
                   gpointer user_data)
{
  guint rv = 0;
  GSList *l;

  if (!cb)
  {
    ILOG_ERR("iap is NULL when requesting ip info");
    return 0;
  }

  if (iap->state != ICD_IAP_STATE_CONNECTED)
  {
    ILOG_INFO("iap %p in state %s does not have ip info available", iap,
              icd_iap_state_names[iap->state]);
    return 0;
  }

  for (l = iap->network_modules; l; l = l->next)
  {
    struct icd_network_module *module = (struct icd_network_module *)l->data;

    if (module)
    {
      if (module->nw.ip_addr_info)
      {
        rv++;
        ILOG_INFO("iap %p module '%s' has address info", iap, module->name);
        module->nw.ip_addr_info(iap->connection.network_type,
                                iap->connection.network_attrs,
                                iap->connection.network_id, &module->nw.private,
                                cb, user_data);
      }
    }
    else
      ILOG_WARN("iap %p has NULL network module", iap);
  }

  if (!rv)
    ILOG_INFO("iap %p does not have any module with address info", iap);

  return rv;
}

/**
 * Get ip level statistics from an IAP.
 *
 * @param iap        the IAP
 * @param cb         callback function
 * @param user_data  user data
 *
 * @return  TRUE if callback will be called, FALSE otherwise
 */
gboolean
icd_iap_get_ip_stats(struct icd_iap *iap, icd_nw_ip_stats_cb_fn cb,
                     gpointer user_data)
{
  GSList *l;

  if (!cb)
  {
    ILOG_ERR("cb is NULL when requesting ip statistics");
    return FALSE;
  }

  if (iap->state != ICD_IAP_STATE_CONNECTED)
  {
    ILOG_INFO("iap %p in state %s does not have ip info available", iap,
              icd_iap_state_names[iap->state]);
    return FALSE;
  }

  for (l = iap->network_modules; l; l = l->next)
  {
    struct icd_network_module *module = (struct icd_network_module *)l->data;

    if (!module)
      ILOG_WARN("iap %p has NULL network module", iap);
    else if (module->nw.ip_stats)
    {
      ILOG_INFO("iap %p module '%s' has ip statistics", iap, module->name);

      module->nw.ip_stats(iap->connection.network_type,
                          iap->connection.network_attrs,
                          iap->connection.network_id, &module->nw.private,
                          cb, user_data);

      return TRUE;
    }
  }

  cb(user_data, iap->connection.network_type, iap->connection.network_attrs,
     iap->connection.network_id, 0, 0, 0);

  return TRUE;
}

/**
 * Get link level statistics from an IAP.
 *
 * @param iap        the IAP
 * @param cb         callback function
 * @param user_data  user data
 *
 * @return  TRUE if callback will be called, FALSE otherwise
 */
gboolean
icd_iap_get_link_stats(struct icd_iap *iap, icd_nw_link_stats_cb_fn cb,
                       gpointer user_data)
{
  GSList *l;

  if (!cb)
  {
    ILOG_ERR("cb is NULL when requesting link statistics");
    return FALSE;
  }

  if (iap->state != ICD_IAP_STATE_CONNECTED)
  {
    ILOG_INFO("iap %p in state %s cannot provide statistics", iap,
              icd_iap_state_names[iap->state]);
    return FALSE;
  }

  for (l = iap->network_modules; l; l= l->next)
  {
    struct icd_network_module * module = (struct icd_network_module *)l->data;
    if (!module)
      ILOG_WARN("iap %p has NULL network module", iap);
    else if (module->nw.link_stats)
    {
      ILOG_INFO("iap %p module '%s' has link statistics", iap, module->name);
      module->nw.link_stats(iap->connection.network_type,
                            iap->connection.network_attrs,
                            iap->connection.network_id,
                            &module->nw.private, cb, user_data);
      return TRUE;
    }
  }

  cb(user_data, iap->connection.network_type, iap->connection.network_attrs,
     iap->connection.network_id, 0, 0, 0, 0, 0, 0);

  return TRUE;
}

/**
 * Get link post level statistics from an IAP.
 *
 * @param iap        the IAP
 * @param cb         callback function
 * @param user_data  user data
 *
 * @return  TRUE if callback will be called, FALSE otherwise
 */
gboolean
icd_iap_get_link_post_stats(struct icd_iap *iap,
                            icd_nw_link_post_stats_cb_fn cb, gpointer user_data)
{
  GSList *l;

  if (!cb)
  {
    ILOG_ERR("cb is NULL when requesting link post statistics");
    return FALSE;
  }

  if (iap->state != ICD_IAP_STATE_CONNECTED)
  {
    ILOG_INFO("iap %p in state %s cannot provide link post stats", iap,
              icd_iap_state_names[iap->state]);
    return FALSE;
  }

  for (l = iap->network_modules; l; l = l->next)
  {
    struct icd_network_module *module = (struct icd_network_module *)l->data;

    if (!module)
      ILOG_WARN("iap %p has NULL network module", iap);
    else if (module->nw.link_post_stats)
    {
      ILOG_INFO("iap %p module '%s' has link post statistics", iap,
                module->name);
      module->nw.link_post_stats(iap->connection.network_type,
                                 iap->connection.network_attrs,
                                 iap->connection.network_id,
                                 &module->nw.private, cb, user_data);
      return TRUE;
    }
  }

  cb(user_data, iap->connection.network_type, iap->connection.network_attrs,
     iap->connection.network_id, 0, 0, 0);

  return TRUE;
}

/**
 * Post-down script has run, restart IAP or report final status.
 *
 * @param pid         the process id of the script that exited
 * @param exit_value  exit value of the script or -1 on timeout
 * @param user_data   current IAP
 */
static void
icd_iap_post_down_script_done(const pid_t pid, const gint exit_value,
                              gpointer user_data)
{
  struct icd_iap *iap = (struct icd_iap *)user_data;

  iap->script_pids = g_slist_remove(iap->script_pids, GINT_TO_POINTER(pid));

  if (iap->script_pids)
    ILOG_INFO("more post-down scripts still to come, waiting");
  else
  {
    switch (iap->state)
    {
      case ICD_IAP_STATE_IP_RESTART_SCRIPTS:
      case ICD_IAP_STATE_LINK_PRE_RESTART_SCRIPTS:
      case ICD_IAP_STATE_LINK_RESTART_SCRIPTS:
        ILOG_DEBUG("iap %p in state %s post down scripts run, now run pre-up",
                   iap, icd_iap_state_names[iap->state]);
        icd_iap_run_pre_up_scripts(iap);
        break;
      case ICD_IAP_STATE_SCRIPT_POST_DOWN:
        iap->state = ICD_IAP_STATE_DISCONNECTED;

        if (!icd_iap_run_restart(iap))
        {
          if (iap->busy)
          {
            if (iap->err_str)
            {
              ILOG_DEBUG("iap %p being busy is not an error, clearing error string",
                         iap);
              g_free(iap->err_str);
              iap->err_str = NULL;
            }

            icd_iap_do_callback(ICD_IAP_BUSY, iap);
          }
          else if (iap->err_str)
          {
            ILOG_INFO("iap disconnected, error is '%s'", iap->err_str);
            icd_iap_do_callback(ICD_IAP_FAILED, iap);
          }
          else
          {
            ILOG_INFO("iap disconnected cleanly");
            icd_iap_do_callback(ICD_IAP_DISCONNECTED, iap);
          }
        }
        break;
      default:
        ILOG_ERR("iap %p in state %s when post down scripts are run, filea a bug",
                 iap, icd_iap_state_names[iap->state]);
        break;
    }
  }
}

static void
icd_iap_run_post_down_scripts(struct icd_iap *iap)
{
  GSList *env = iap->script_env;
  gchar *id = NULL;

  if (iap->id && !iap->id_is_local)
    id = gconf_escape_key(iap->id, -1);

  if (!env)
  {
    pid_t pid;

    ILOG_INFO("no env vars for post-down script");
    pid = icd_script_post_down(iap->interface_name, id,
                               iap->connection.network_type,
                               NULL, icd_iap_post_down_script_done, iap);

    iap->script_pids = g_slist_prepend(iap->script_pids, GINT_TO_POINTER(pid));
  }
  else
  {
    while (env)
    {
      if (env->data)
      {
        pid_t pid = icd_script_post_down(iap->interface_name, id,
                                         iap->connection.network_type,
                                         (const struct icd_iap_env *)env->data,
                                         icd_iap_post_down_script_done, iap);
        iap->script_pids = g_slist_prepend(iap->script_pids,
                                           GINT_TO_POINTER(pid));
      }

      env = env->next;
    }
  }

  g_free(id);
}

/**
 * Check whether the iap needs to be initiated. Called when the network
 * module layer disconnect functions have been exhausted and when post down
 * scripts have been run.
 *
 * @param iap  the IAP
 * @return     TRUE if a restart was initiated, FALSE if not
 */
static gboolean
icd_iap_run_restart(struct icd_iap *iap)
{
  enum icd_iap_state state = iap->state;
  enum icd_iap_state next_state;

  if (state == ICD_IAP_STATE_IP_DOWN)
  {
    if (iap->restart_layer != ICD_NW_LAYER_IP)
      return FALSE;

    next_state = ICD_IAP_STATE_IP_UP;
  }
  else if (state <= ICD_IAP_STATE_IP_DOWN)
  {
    if (state == ICD_IAP_STATE_DISCONNECTED ||
        iap->restart_layer != ICD_NW_LAYER_ALL)
    {
      return FALSE;
    }

    next_state = ICD_IAP_STATE_LINK_UP;
  }
  else if ( state == ICD_IAP_STATE_LINK_PRE_DOWN )
  {
    if (iap->restart_layer != ICD_NW_LAYER_LINK_POST)
      return FALSE;

    next_state = ICD_IAP_STATE_LINK_POST_UP;
  }
  else
  {
    if (state != ICD_IAP_STATE_LINK_DOWN ||
        iap->restart_layer != ICD_NW_LAYER_LINK)
    {
      return FALSE;
    }

    next_state = ICD_IAP_STATE_LINK_UP;
  }

  iap->restart_count++;

  if (icd_policy_api_iap_restart(&iap->connection, iap->restart_count) ==
      ICD_POLICY_REJECTED)
  {
    ILOG_ERR("ICD_NW_RESTART requested %d times for iap %p, restart limit exceed",
             iap->restart_count, iap);

    if (iap->state == ICD_IAP_STATE_DISCONNECTED)
    {
      icd_iap_do_callback(ICD_IAP_FAILED, iap);
      return TRUE;
    }

    ILOG_DEBUG("iap %p is going to continue disconnecting", iap);

    return FALSE;
  }

  if ( icd_log_get_level() == ICD_DEBUG )
    ILOG_DEBUG("restart for layer %s was requested, iap %p starting to (re)connect in state %s, next state %s",
               icd_iap_layer_names[iap->restart_layer], iap,
               icd_iap_state_names[iap->state],
               icd_iap_state_names[next_state]);

  if (iap->err_str)
  {
    ILOG_DEBUG("iap %p being restarted is not an error, clearing error string",
               iap);

    g_free(iap->err_str);
    iap->err_str = NULL;
  }

  iap->restart_layer = ICD_NW_LAYER_NONE;
  iap->restart_state = ICD_IAP_STATE_DISCONNECTED;
  iap->state = next_state;

  if (next_state == ICD_IAP_STATE_LINK_PRE_RESTART_SCRIPTS ||
      next_state == ICD_IAP_STATE_LINK_RESTART_SCRIPTS ||
      next_state == ICD_IAP_STATE_IP_RESTART_SCRIPTS )
  {
    icd_iap_run_post_down_scripts(iap);
  }
  else
  {
    icd_iap_modules_reset(iap);
    icd_iap_module_next(iap);
  }

  return TRUE;
}

/**
 * Create a new unique id for the iap, settings are accessed using this id
 *
 * @param iap     the IAP
 * @param new_id  preferably NULL but can also be the new id
 *
 * @return        TRUE on success, FALSE on failure
 */
gboolean
icd_iap_id_create(struct icd_iap *iap, const gchar *new_name)
{
  GConfClient *gconf;
  GError *error = NULL;
  gchar *uuid = NULL;
  int tries = 0;

  if (iap->id)
  {
    g_free(iap->id);
    iap->id = NULL;
  }

  iap->id_is_local = FALSE;

  if (new_name)
  {
    iap->id = g_strdup(new_name);
    return TRUE;
  }

  if (iap->connection.network_attrs & ICD_NW_ATTR_IAPNAME &&
      iap->connection.network_id)
  {
    iap->id = g_strdup(iap->connection.network_id);
    return TRUE;
  }

  tries = 0;
  gconf = gconf_client_get_default();

  while (1)
  {
    gchar *s;
    gchar *dir;
    gboolean exists;

    tries++;

    if (!g_file_get_contents("/proc/sys/kernel/random/uuid", &uuid, NULL,
                             &error))
    {
      break;
    }

    if (uuid)
      g_strchomp(g_strchug(uuid));

    s = gconf_escape_key(uuid, -1);
    dir = g_strconcat(ICD_GCONF_PATH, "/", s, NULL);
    g_free(s);

    exists = gconf_client_dir_exists(gconf, dir, NULL);
    g_free(dir);

    if (exists)
    {
      g_free(uuid);
      uuid = NULL;

      if (tries != 10)
        continue;
    }

    g_object_unref(gconf);

    if (uuid)
    {
      iap->id = uuid;
      iap->id_is_local = TRUE;
    }
    else
      ILOG_ERR("iap->id cannot be generated");

    return TRUE;
  }

  ILOG_ERR("Unable to read file: %s", error ? error->message : "");

  g_object_unref(gconf);

  if (uuid)
    g_free(uuid);

  return FALSE;
}

/** @} */
