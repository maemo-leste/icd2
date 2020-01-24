/**
@file policy_always_online.c
@copyright GNU GPLv2 or later

@addtogroup policy_always_online Always online policy
@ingroup policy

 * @{ */

#include <gconf/gconf-client.h>
#include <osso-ic-gconf.h>
#include <osso-ic.h>
#include <mce/dbus-names.h>
#include <mce/mode-names.h>

#include <string.h>

#include "policy_api.h"
#include "icd_dbus.h"
#include "icd_log.h"


#define CHANGE_WHILE_CONNECTED_KEY ICD_GCONF_NETWORK_MAPPING \
                                                "/change_while_connected"

#define AUTO_CONNECT_KEY ICD_GCONF_NETWORK_MAPPING    "/auto_connect"

#define SEARCH_INTERVAL_KEY ICD_GCONF_NETWORK_MAPPING "/search_interval"

/** how long to wait befor counting IAPs after gconf has changed */
#define POLICY_ALWAYS_ONLINE_IAP_TIMEOUT   500

/** extra filter for mce signals */
#define POLICY_ALWAYS_ONLINE_MCE_FILTER   "member='" MCE_DEVICE_MODE_SIG "'"

/** timeout for MCE method call */
#define POLICY_ALWAYS_ONLINE_MCE_TIMEOUT   5000


/** private data for the always online policy */
struct always_online_data {
  /** number of connections ongoing */
  guint connection_count;
  /** timeout callback id for IAP counting */
  guint count_iaps_id;
  /** number of IAPs in GConf */
  guint iap_count;

  /** timeout value in minutes */
  gint timeout;
  /** timer id */
  guint timeout_id;

  /** whether connect automatically has a decent value */
  gboolean auto_conn;
  /** whether to make a connection attempt even when connected */
  gboolean always_change;
  /** whether any always online settings values got changed */
  gboolean always_online_value_changed;

  /** GConf notification id for network parameters */
  guint notify_nw_params;
  /** GConf notification id for connections */
  guint notify_connections;

  /** TRUE if in flight mode, FALSE if in online mode */
  gboolean flight_mode;
  /** flight mode pending call */
  DBusPendingCall *pending_flightmode;
  /** TRUE if broadcast signals have been connected */
  gboolean flightmode_signals;

  /** function to request OSSO_IAP_ANY */
  icd_policy_request_make_new_fn make_request;
  /** function to request network priority information */
  icd_policy_network_priority_fn priority;
  /** function to check if there is a service module for a given network type */
  icd_policy_service_module_check_fn srv_check;

  /** Highest priority network currently connected (this acts as a boolean
   * flag but for debug purposes we want to know the actual priority). If set
   * to 0 then this is not the highest, otherwise the highest priority value */
  gint highest_network_priority;
};


static void policy_always_online_run(struct always_online_data *data,
                                     gboolean immediately);

/**
 * Count how many IAPs there are in GConf
 * @return  number of IAPs
 */
static guint
policy_always_online_count_iaps()
{
  GConfClient *gconf = gconf_client_get_default();
  GSList *l;
  int count = 0;

  l = gconf_client_all_dirs(gconf, ICD_GCONF_PATH, NULL);

  while (l)
  {
    GSList *next;

    g_free(l->data);
    count++;
    next = g_slist_remove_link(l, l);
    g_slist_free_1(l);
    l = next;
  }

  g_object_unref(gconf);

  return count;
}

/**
 * Cancel pending call to MCE
 * @param data  always online policy data
 */
static void
policy_always_online_cancel_pending(struct always_online_data *data)
{
  if (data->pending_flightmode)
  {
    ILOG_INFO("always online cancelling pending call");
    dbus_pending_call_cancel(data->pending_flightmode);
    data->pending_flightmode = 0;
  }
}

/**
 * Cancel always online timer
 * @param data  always online policy data
 */
static void
policy_always_online_cancel_timer(struct always_online_data *data)
{
  if (data->timeout_id)
  {
    ILOG_INFO("always online timeout id %d cancelled",
              data->timeout_id);
    g_source_remove(data->timeout_id);
    data->timeout_id = 0;
  }
}

/**
 * Make a request for OSSO_IAP_ANY, no user prompting
 * @param data  always online policy data
 */
static void
policy_always_online_make_request(struct always_online_data *data)
{
  guint policy_attrs = ICD_POLICY_ATTRIBUTE_NO_INTERACTION |
                       ICD_POLICY_ATTRIBUTE_ALWAYS_ONLINE;

  ILOG_INFO("always online making new request");

  if (data->always_change)
    policy_attrs |= ICD_POLICY_ATTRIBUTE_ALWAYS_ONLINE_CHANGE;

  data->make_request(policy_attrs, NULL, 0, NULL, NULL, 0, OSSO_IAP_ANY);
}

/**
 * Timeout callback for a new request
 * @param user_data  always online policy data
 * @return  TRUE to run again
 */
static gboolean
policy_always_online_make_request_cb(gpointer user_data)
{
  struct always_online_data *data =
      (struct always_online_data *)user_data;

  ILOG_DEBUG("always online timer %d triggered", data->timeout_id);
  policy_always_online_run(data, TRUE);

  return TRUE;
}

/**
 * Parse flight mode message
 * @param message  the D-Bus message
 * @param data     always online policy data
 */
static void
policy_always_online_flightmode(DBusMessage *message,
                                struct always_online_data *data)
{
  gboolean flight_mode;
  gchar *mode;

  if (dbus_message_get_args(message, NULL,
                            DBUS_TYPE_STRING, &mode,
                            DBUS_TYPE_INVALID))
  {
    if (!strcmp(mode, MCE_FLIGHT_MODE) || !strcmp(mode, MCE_OFFLINE_MODE))
    {
      flight_mode = TRUE;

      if (!data->flight_mode)
      {
        ILOG_INFO("always online: offline mode");
        policy_always_online_cancel_timer(data);
      }
    }
    else
    {
      flight_mode = FALSE;

      if (data->flight_mode)
      {
        ILOG_INFO("always online: normal mode");
        policy_always_online_cancel_timer(data);
        data->timeout_id =
            g_timeout_add(2000, policy_always_online_make_request_cb, data);

        ILOG_INFO("always online waiting 2s for the normal mode to propagate through the rest of the system, timer id is %d",
                  data->timeout_id);
      }
    }

    if (flight_mode == data->flight_mode)
    {
      ILOG_DEBUG("always online received same state, %s",
                 flight_mode ? "offline mode" : "normal mode");
    }
    else
      data->flight_mode = flight_mode;
  }
  else
    ILOG_ERR("always online could not parse flight mode message");
}

/**
 * Flight mode signal handling function; used also by
 * policy_always_online_flightmode_cb()
 *
 * @param connection  D-Bus connection or NULL if called from
 *                    policy_always_online_flightmode_cb()
 * @param message     D-Bus flight mode status message
 * @param user_data   always online policy data
 *
 * @return  DBUS_HANDLER_RESULT_NOT_YET_HANDLED as some other part of this
 *          program might also be interested in the signal
 */
static DBusHandlerResult
policy_always_online_flightmode_sig(DBusConnection *connection,
                                    DBusMessage *message,
                                    void *user_data)
{
  if (dbus_message_is_signal(message, MCE_SIGNAL_IF, MCE_DEVICE_MODE_SIG))
  {
    policy_always_online_flightmode(message,
                                    (struct always_online_data *)user_data);
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * Flight mode status pending call callback
 * @param pending    the pending call
 * @param user_data  always online policy data
 */
static void
policy_always_online_flightmode_cb(DBusPendingCall *pending, void *user_data)
{
  struct always_online_data *data =
      (struct always_online_data *)user_data;
  DBusMessage *message = dbus_pending_call_steal_reply(pending);

  dbus_pending_call_unref(data->pending_flightmode);
  data->pending_flightmode = NULL;
  policy_always_online_flightmode(message, data);
  dbus_message_unref(message);
}

/**
 * Check if always online is set and run either immediately or set a timeout
 * according to settings
 *
 * @param data         always online policy data
 * @param immediately  whether to start immediately or after a timeout
 */
static void
policy_always_online_run(struct always_online_data *data,
                         gboolean immediately)
{
  gboolean module_loaded;

  data->always_online_value_changed = FALSE;

  if (data->timeout_id)
    policy_always_online_cancel_timer(data);

  module_loaded = data->srv_check(NULL);

  if ((!data->iap_count && !module_loaded) ||
      !data->auto_conn || data->timeout <= 0 ||
      (data->connection_count && !data->always_change) ||
      data->highest_network_priority )
  {
    ILOG_DEBUG("always online doesn't run because iap count %d <= 0; "
               "srv_provider %s; auto_conn is '%s'; timeout %d <= 0; number of "
               "connections %d > 0 and always change %s; priority %d",
               data->iap_count, module_loaded ? "TRUE" : "FALSE",
               data->auto_conn ? "TRUE" : "FALSE", data->timeout,
               data->connection_count, data->always_change ? "TRUE" : "FALSE",
               data->highest_network_priority);

    return;
  }

  ILOG_DEBUG("always online run because iap count %d; srv_provider %s; "
             "auto_conn is '%s'; timeout %d; number of connections %d and "
             "always change %s; priority %d", data->iap_count,
             module_loaded ? "TRUE" : "FALSE",
             data->auto_conn? "TRUE" : "FALSE", data->timeout,
             data->connection_count, data->always_change ? "TRUE" : "FALSE",
             data->highest_network_priority);

  if (immediately)
    policy_always_online_make_request(data);

  data->timeout_id = g_timeout_add(1000 * data->timeout,
                                   policy_always_online_make_request_cb, data);

  ILOG_INFO("always online timer id %d added", data->timeout_id);
}

/**
 * Check the number of IAPs in gconf and whether always online gconf settings
 * were changed. Connect automatically when needed
 *
 * @param user_data  always_online_policy_data
 * @return           FALSE;
 */
static gboolean
policy_always_online_check(gpointer user_data)
{
  struct always_online_data *data =
      (struct always_online_data *)user_data;
  int iap_count = policy_always_online_count_iaps();

  ILOG_DEBUG("always online found %d IAPs in gconf", iap_count);

  data->count_iaps_id = 0;

  if (!data->iap_count && iap_count)
  {
    data->iap_count = iap_count;
    ILOG_INFO("always online found one newly created IAP in gconf, "
              "trying to activate");
  }
  else
  {
    data->iap_count = iap_count;

    if (!data->always_online_value_changed)
      return FALSE;

    ILOG_INFO("always online values changed, trying to activate");
  }

  policy_always_online_run(data, TRUE);

  return FALSE;
}

/**
 * Notice a changed value in gconf
 *
 * @param client     GConf client
 * @param cnxn_id    connection id
 * @param entry      GConf entry
 * @param user_data  always online policy data
 */
static void
policy_always_online_nw_params_changed(GConfClient *client,
                                       guint cnxn_id,
                                       GConfEntry *entry,
                                       gpointer user_data)
{
  struct always_online_data *data =
      (struct always_online_data *)user_data;
  const char *key = gconf_entry_get_key(entry);

  if (key)
  {
    GConfValue *val = gconf_entry_get_value(entry);

    if (!strcmp(key, AUTO_CONNECT_KEY))
    {
      if (G_VALUE_HOLDS(val, GCONF_VALUE_LIST))
        data->auto_conn = !!gconf_value_get_list(val);
      else
        data->auto_conn = FALSE;

      ILOG_INFO("always online connect automatically: %s",
                data->auto_conn ? "yes" : "no");

      data->always_online_value_changed = TRUE;
    }
    else if (!strcmp(key, SEARCH_INTERVAL_KEY))
    {
      if (G_VALUE_HOLDS(val, GCONF_VALUE_INT))
        data->timeout = gconf_value_get_int(val);
      else
        data->timeout = 0;

      ILOG_INFO("always online timer set to %d seconds(s)",
                data->timeout);
      data->always_online_value_changed = TRUE;
    }
    else if (!strcmp(key, CHANGE_WHILE_CONNECTED_KEY))
    {
      if (G_VALUE_HOLDS(val, GCONF_VALUE_BOOL))
        data->always_change = gconf_value_get_bool(val);
      else
        data->always_change = FALSE;

      ILOG_DEBUG("always online run while connected: %s",
                 data->always_change ? "yes" : "no");

      data->always_online_value_changed = TRUE;
    }
  }

  if (!data->count_iaps_id)
  {
    data->count_iaps_id = g_timeout_add(POLICY_ALWAYS_ONLINE_IAP_TIMEOUT,
                                        policy_always_online_check, data);
  }
}

/**
 * Notice a changed value in gconf
 *
 * @param client     GConf client
 * @param cnxn_id    connection id
 * @param entry      GConf entry
 * @param user_data  always online policy data
 */
static void
policy_always_online_connections_changed(GConfClient *client,
                                         guint cnxn_id,
                                         GConfEntry *entry,
                                         gpointer user_data)
{
  struct always_online_data *data =
      (struct always_online_data *)user_data;

  if (gconf_entry_get_value(entry) && !data->count_iaps_id)
  {
    ILOG_DEBUG("always online will soon check whether connections were added");

    data->count_iaps_id = g_timeout_add(POLICY_ALWAYS_ONLINE_IAP_TIMEOUT,
                                        policy_always_online_check, data);
  }
}

/**
 * Policy module destruction function. Will be called before unloading the
 * module.
 *
 * @param private  a reference to the private data
 */
static void
policy_always_online_destruct(gpointer *private)
{
  struct always_online_data *data =
      (struct always_online_data *)*private;
  GConfClient *gconf = gconf_client_get_default();

  policy_always_online_cancel_pending(data);

  if (data->flightmode_signals)
  {
    icd_dbus_disconnect_system_bcast_signal(
        MCE_SIGNAL_IF, policy_always_online_flightmode_sig, data,
        POLICY_ALWAYS_ONLINE_MCE_FILTER);
  }

  if (data->notify_nw_params)
  {
    gconf_client_remove_dir(gconf, ICD_GCONF_NETWORK_MAPPING, NULL);
    gconf_client_notify_remove(gconf, data->notify_nw_params);
  }

  if (data->notify_connections)
  {
    gconf_client_remove_dir(gconf, ICD_GCONF_PATH, NULL);
    gconf_client_notify_remove(gconf, data->notify_connections);
  }

  g_object_unref(gconf);
  policy_always_online_cancel_timer(data);

  if (data->count_iaps_id)
  {
    ILOG_INFO("always online iap count id %d cancelled",
              data->count_iaps_id);
    g_source_remove(data->count_iaps_id);
    data->count_iaps_id = 0;
  }

  g_free(data);
  *private = NULL;
}

/**
 * Informational policy called when a network has been disconnected
 *
 * @param network               the network to connect
 * @param err_str               NULL if the network was disconnected
 *                              normally, any ICD_DBUS_ERROR_* from
 *                              osso-ic-dbus.h on error
 * @param existing_connections  existing network connections
 * @param private               private data
 */
static void
policy_always_online_disconnected(struct icd_policy_request *network,
                                  const gchar *err_str,
                                  GSList *existing_connections,
                                  gpointer *private)
{
  struct always_online_data *data =
      (struct always_online_data *)*private;

  if (data->connection_count)
  {
    data->connection_count--;

    ILOG_DEBUG("always online connection count %d",
               data->connection_count);

    if (!data->connection_count)
      data->highest_network_priority = 0;

    policy_always_online_run(data, err_str ? TRUE : FALSE);
  }
  else
    ILOG_DEBUG("always online sees network disconnected, but none connected");
}

/**
 * Informational policy called when a network has been successfully connected
 *
 * @param network               the network to connect
 * @param existing_connections  existing network connections
 * @param private               private data
 */
static void
policy_always_online_connected(struct icd_policy_request *network,
                               GSList *existing_connections,
                               gpointer *private)
{
  struct always_online_data *data =
      (struct always_online_data *)*private;
  gint highest_network_priority;

  data->connection_count = g_slist_length(existing_connections);

  ILOG_DEBUG("always online connection count %d", data->connection_count);

  if (data->always_change && data->priority)
  {
    if (data->priority(network->service_type, network->service_id,
                       network->network_type, network->network_attrs,
                       &highest_network_priority) )
    {
      ILOG_DEBUG("always online timer not cancelled because higher priority network exists and always_change is set (%s/0x%04x/%s/%d)",
                 network->network_type, network->network_attrs,
                 network->network_id, highest_network_priority);
      data->highest_network_priority = 0;
    }
    else
    {
      policy_always_online_cancel_timer(data);
      data->highest_network_priority = highest_network_priority;
    }
  }
  else
    policy_always_online_cancel_timer(data);
}

/**
 * Initialize flight mode information fetching from MCE
 * @param data  always online policy data
 * @return      TRUE on success, FALSE on failure
 */
static gboolean
policy_always_online_flightmode_init(struct always_online_data *data)
{
  DBusMessage *message = dbus_message_new_method_call(
      MCE_SERVICE, MCE_REQUEST_PATH, MCE_REQUEST_IF, MCE_DEVICE_MODE_GET);

  if (!message)
  {
    ILOG_CRIT("always online could not create flightmode request message");
    return FALSE;
  }

  data->pending_flightmode = icd_dbus_send_system_mcall(
        message, POLICY_ALWAYS_ONLINE_MCE_TIMEOUT,
        policy_always_online_flightmode_cb, data);

  if (!data->pending_flightmode)
  {
    ILOG_CRIT("always online could not send flightmode request");
    dbus_message_unref(message);
    return FALSE;
  }

  return TRUE;
}

/**
 * Initialize GConf notifications
 * @param data  always online policy data
 * @return      TRUE on success, FALSE on failure
 */
static gboolean
policy_always_online_gconf_init(struct always_online_data *data)
{
  GConfClient *gconf = gconf_client_get_default();
  GError *error = NULL;

  data->notify_nw_params = gconf_client_notify_add(
      gconf, ICD_GCONF_NETWORK_MAPPING, policy_always_online_nw_params_changed,
      data, NULL, &error);

  if (!error)
  {
    data->notify_connections = gconf_client_notify_add(
          gconf, ICD_GCONF_PATH, policy_always_online_connections_changed,
          data, NULL, &error);
  }

  if (error)
  {
    ILOG_ERR("always online gconf notification error: %s", error->message);
    g_clear_error(&error);
    g_object_unref(gconf);
    return FALSE;
  }

  gconf_client_add_dir(gconf, ICD_GCONF_NETWORK_MAPPING,
                       GCONF_CLIENT_PRELOAD_ONELEVEL, &error);

  if (!error)
  {
    gconf_client_add_dir(gconf, ICD_GCONF_PATH,
                         GCONF_CLIENT_PRELOAD_ONELEVEL, &error);
  }

  if (error)
  {
    ILOG_ERR("always online gconf add dir error: %s", error->message);
    g_clear_error(&error);
    g_object_unref(gconf);
    return FALSE;
  }

  g_object_unref(gconf);
  return TRUE;
}

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
  GConfClient *gconf = gconf_client_get_default();
  struct always_online_data *data =
      g_new0(struct always_online_data, 1);
  GConfValue *val;

  policy_api->private = data;

  data->make_request = make_request;
  data->flight_mode = TRUE;

  val = gconf_client_get(gconf, AUTO_CONNECT_KEY, NULL);
  data->auto_conn = G_VALUE_HOLDS(val, GCONF_VALUE_LIST) &&
      gconf_value_get_list(val);

  ILOG_INFO("always online connect automatically: %s",
            data->auto_conn ? "Yes" : "No");

  data->always_change = gconf_client_get_bool(gconf,
                                              CHANGE_WHILE_CONNECTED_KEY, NULL);

  ILOG_INFO("always online connection change: %s",
            data->always_change ? "Yes" : "No");

  data->timeout = gconf_client_get_int(gconf, SEARCH_INTERVAL_KEY, NULL);

  ILOG_INFO("always online timeout defaults to %d minute(s)",
            data->timeout);

  data->iap_count = policy_always_online_count_iaps();

  ILOG_INFO("always online defaults to %d IAPs in gconf", data->iap_count);

  g_object_unref(gconf);

  data->flightmode_signals = icd_dbus_connect_system_bcast_signal(
        MCE_SIGNAL_IF, policy_always_online_flightmode_sig, data,
        POLICY_ALWAYS_ONLINE_MCE_FILTER);

  if (!data->flightmode_signals ||
      !policy_always_online_flightmode_init(data) ||
      !policy_always_online_gconf_init(data) )
  {
    ILOG_CRIT("always online failed to connect, always online disabled");
    policy_always_online_destruct((gpointer *)&data);
    return;
  }

  data->priority = priority;
  data->srv_check = srv_check;
  policy_api->connected = policy_always_online_connected;
  policy_api->disconnected = policy_always_online_disconnected;
  policy_api->destruct = policy_always_online_destruct;
  policy_api->priority = priority;
}

/** @} */
