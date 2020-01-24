/**
@file icd_dbus_api.c
@copyright GNU GPLv2 or later

@addtogroup icd_dbus_api ICd2 D-Bus API implementation
@ingroup internal

 * @{ */

#include <string.h>
#include <dbus/dbus.h>
#include <osso-ic.h>
#include "icd_dbus_api.h"
#include "icd_dbus.h"
#include "icd_log.h"
#include "icd_scan.h"
#include "icd_network_api.h"
#include "icd_context.h"
#include "icd_request.h"
#include "icd_name_owner.h"
#include "icd_gconf.h"

struct icd_dbus_api_addrinfo_data {
  DBusMessage *message;
  DBusMessageIter iter1;
  DBusMessageIter iter2;
  guint total;
  guint called;
};

/** structure for statistics gathering */
struct icd_dbus_api_statistics_data {
  /** D-Bus sender */
  gchar *sender;

  /** Time active */
  guint time_active;

  /** Signal strength */
  gint signal;

  /** Station id, e.g. MAC address */
  gchar *station_id;

  /** Raw dB value */
  gint dB;

  /** Bytes received */
  guint rx_bytes;

  /** Bytes sent */
  guint tx_bytes;
};

/** ICd2 D-Bus API data structure */
struct icd_dbus_api_listeners {
  /** dbus apps receiving scan results */
  GSList *scan_listeners;
};

/** Helper structure for starting a scan */
struct icd_dbus_api_scan_helper {
  /** The reply message iterator */
  DBusMessageIter *reply_str_iter;

  /** scan listener */
  gchar *dbus_dest;

  /** passive/active scan type */
  guint scan_type;
};

struct icd_dbus_api_foreach_data;
/**
 * Template for the D-Bus data sending function
 *
 * @param iap           the IAP
 * @param foreach_data  foreach data structure
 *
 * @return  TRUE on success, FALSE on error
 */
typedef gboolean(* icd_dbus_api_foreach_send_fn)(
    struct icd_iap *iap, struct icd_dbus_api_foreach_data *foreach_data);

/** data structure to keep track of the number of connections and the sender */
struct icd_dbus_api_foreach_data {
  /** number of connections */
  guint connections;

  /** the D-Bus sender or NULL */
  const gchar *sender;

  /** function that sends data to D-Bus applications */
  icd_dbus_api_foreach_send_fn send_fn;
};

static DBusHandlerResult icd_dbus_api_state_req(DBusConnection *conn,
                                                DBusMessage *msg,
                                                void *user_data);

static gboolean
icd_dbus_api_foreach_iap_req(DBusMessage *message,
                             struct icd_dbus_api_foreach_data *foreach_data);

/**
 * Handle cancelling of scans
 *
 * @param conn       D-Bus connection
 * @param msg        D-Bus message
 * @param user_data  not used
 */
static DBusHandlerResult
icd_dbus_api_scan_cancel(DBusConnection *conn, DBusMessage *msg,
                         void *user_data)
{
  DBusMessage *reply;

  if (icd_dbus_api_app_exit(dbus_message_get_sender(msg)))
    reply = dbus_message_new_method_return(msg);

  else
  {
    reply = dbus_message_new_error(msg, DBUS_ERROR_FAILED,
                                   "Scan results have not been requested");
  }

  if (!reply)
  {
    reply = dbus_message_new_error(msg, DBUS_ERROR_NO_MEMORY,
                                   "Out of memory when creating reply");

    if (!reply)
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  icd_dbus_send_system_msg(reply);
  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;
}

/**
 * Receive IP address configuration information based on network type,
 * attributes and id.
 *
 * @param addr_info_cb_token  token passed to the request function
 * @param network_type        network type
 * @param network_attrs       attributes, such as type of network_id,
 *                            security, etc.
 * @param network_id          IAP name or local id, e.g. SSID
 * @param private             a reference to the icd_nw_api private member
 * @param ip_address          IP address string or NULL if no such value
 * @param ip_netmask          IP netmask string which or NULL if no such
 *                            value
 * @param ip_gateway          IP gateway string which or NULL if no such
 *                            value
 * @param ip_dns1             DNS server IP address string or NULL if no such
 *                            value
 * @param ip_dns2             DNS server IP address string or NULL if no such
 *                            value
 * @param ip_dns3             DNS server IP address string or NULL if no such
 *                            value
 *
 * @return  TRUE if some of the values are returned, FALSE if no values
 *          assigned
 */
static void
icd_dbus_api_addrinfo_cb(gpointer addr_info_cb_token, const gchar *network_type,
                         const guint network_attrs, const gchar *network_id,
                         gchar *ip_address, gchar *ip_netmask,
                         gchar *ip_gateway,  gchar *ip_dns1, gchar *ip_dns2,
                         gchar *ip_dns3)
{
  DBusMessageIter *iter;
  struct icd_dbus_api_addrinfo_data *addrinfo_data;
  gchar **s;
  DBusMessageIter sub;
  char *empty = "";

  addrinfo_data = (struct icd_dbus_api_addrinfo_data *)addr_info_cb_token;
  iter = &addrinfo_data->iter2;

  if (!dbus_message_iter_open_container(&addrinfo_data->iter2,
                                         DBUS_TYPE_STRUCT , NULL, &sub))
  {
    goto out;
  }

  s = ip_address ? &ip_address : &empty;

  if(!dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, s))
    goto out;

  s = ip_netmask ? &ip_netmask : &empty;

  if (!dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, s))
    goto out;

  s = ip_gateway ? &ip_gateway : &empty;

  if (!dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, s))
    goto out;

  s = ip_dns1 ? &ip_dns1 : &empty;

  if (!dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, s))
    goto out;

  s = ip_dns2 ? &ip_dns2 : &empty;

  if(!dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, s))
    goto out;

  s = ip_dns3 ? &ip_dns3 : &empty;

  if (!dbus_message_iter_append_basic(&sub, DBUS_TYPE_STRING, s) ||
      !dbus_message_iter_close_container(iter, &sub))
  {
    goto out;
  }

  addrinfo_data->called++;

  ILOG_DEBUG("addrinfo called %d/%d times in callback",
             addrinfo_data->called, addrinfo_data->total);

  if (addrinfo_data->called == addrinfo_data->total)
  {
    ILOG_DEBUG("addrinfo close array in cb");

    if (dbus_message_iter_close_container(&addrinfo_data->iter1, iter))
      icd_dbus_send_system_msg(addrinfo_data->message);
  }

out:
  if (addrinfo_data->message)
    dbus_message_unref(addrinfo_data->message);

  g_free(addrinfo_data);
}

/**
 * Function for sending address info data to listeners
 *
 * @param iap           the IAP
 * @param foreach_data  foreach data structure
 *
 * @return  TRUE on successful signal sending, FALSE on error
 */
static gboolean
icd_dbus_api_addrinfo_send(struct icd_iap *iap,
                           struct icd_dbus_api_foreach_data *foreach_data)
{
  struct icd_dbus_api_addrinfo_data *addrinfo_data =
      g_new0(struct icd_dbus_api_addrinfo_data, 1);
  DBusMessage *message;
  const gchar **s;
  const char *network_id;
  DBusMessageIter sub;
  const gchar *empty = "";

  message = dbus_message_new_signal(ICD_DBUS_API_PATH,
                                    ICD_DBUS_API_INTERFACE,
                                    ICD_DBUS_API_ADDRINFO_SIG);

  addrinfo_data->message = message;

  if (message == NULL)
  {
    ILOG_ERR("dbus api out of memory when creating addrinfo signal");
    goto fail;
  }

  if (foreach_data->sender)
    dbus_message_set_destination(message, foreach_data->sender);

  dbus_message_iter_init_append(addrinfo_data->message, &addrinfo_data->iter1);

  s = iap->connection.service_type ?
        (const gchar **)&iap->connection.service_type : &empty;

  if (!dbus_message_iter_append_basic(&addrinfo_data->iter1,
                                      DBUS_TYPE_STRING, s))
  {
    goto fail;
  }

  if (!dbus_message_iter_append_basic(&addrinfo_data->iter1,
                                      DBUS_TYPE_UINT32,
                                      &iap->connection.service_attrs))
  {
    goto fail;
  }

  s = iap->connection.service_id ?
        (const gchar **)&iap->connection.service_id : &empty;

  if (!dbus_message_iter_append_basic(&addrinfo_data->iter1,
                                      DBUS_TYPE_STRING, s))
  {
    goto fail;
  }

  s = iap->connection.network_type ?
        (const gchar **)&iap->connection.network_type : &empty;

  if (!dbus_message_iter_append_basic(&addrinfo_data->iter1,
                                      DBUS_TYPE_STRING, s))
  {
    goto fail;
  }

  if (!dbus_message_iter_append_basic(&addrinfo_data->iter1,
                                      DBUS_TYPE_UINT32,
                                      &iap->connection.network_attrs))
  {
    goto fail;
  }

  network_id = iap->connection.network_id;

  if (!network_id)
    network_id = empty;

  if (!dbus_message_iter_open_container(&addrinfo_data->iter1,
                                        DBUS_TYPE_ARRAY,
                                        DBUS_TYPE_BYTE_AS_STRING,
                                        &sub))
  {
    goto fail;
  }

  if (!dbus_message_iter_append_fixed_array(
           &sub, DBUS_TYPE_BYTE, &network_id, strlen(network_id) + 1))
  {
    goto fail;
  }

  if (!dbus_message_iter_close_container(&addrinfo_data->iter1, &sub) ||
      !dbus_message_iter_open_container(&addrinfo_data->iter1,
                                        DBUS_TYPE_ARRAY,
                                        "(ssssss)",
                                        &addrinfo_data->iter2))
  {
    goto fail;
  }

  addrinfo_data->total = icd_iap_get_ipinfo(
                             iap, icd_dbus_api_addrinfo_cb, addrinfo_data);

  ILOG_DEBUG("addrinfo called %d/%d times",
             addrinfo_data->called, addrinfo_data->total);

  if (addrinfo_data->called != addrinfo_data->total)
    return TRUE;

  ILOG_DEBUG("addrinfo closing array");

  if (dbus_message_iter_close_container(
                  &addrinfo_data->iter1, &addrinfo_data->iter2))
  {
    icd_dbus_send_system_msg(addrinfo_data->message);
    dbus_message_unref(addrinfo_data->message);
    g_free(addrinfo_data);
    return TRUE;
  }

fail:
  if (addrinfo_data->message)
    dbus_message_unref(addrinfo_data->message);

  g_free(addrinfo_data);

  return FALSE;
}

/**
 * Handle addrinfo requests
 *
 * @param conn       D-Bus connection
 * @param msg        D-Bus message
 * @param user_data  dbus client data
 */
static DBusHandlerResult
icd_dbus_api_addrinfo_req(DBusConnection *conn, DBusMessage *msg,
                          void *user_data)
{
  DBusMessage *mreturn = dbus_message_new_method_return(msg);
  struct icd_dbus_api_foreach_data data;

  if (!mreturn)
  {
    ILOG_ERR("dbus api cannot create state req mcall return");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  data.connections = 0;
  data.send_fn = icd_dbus_api_addrinfo_send;
  data.sender = dbus_message_get_sender(msg);
  icd_dbus_api_foreach_iap_req(msg, &data);

  if (dbus_message_append_args(mreturn,
                               DBUS_TYPE_UINT32, &data.connections,
                               DBUS_TYPE_INVALID))
  {
    icd_dbus_send_system_msg(mreturn);
    dbus_message_unref(mreturn);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  dbus_message_unref(mreturn);

  ILOG_ERR("dbus_api could not add args to mcall return");

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * Free a statistics data structure
 * @param stats  the statistics data structure
 */
static void
icd_dbus_api_statistics_data_free(struct icd_dbus_api_statistics_data *stats)
{
  g_free(stats->sender);
  g_free(stats->station_id);
  g_free(stats);
}

/**
 * Receive ip statistics based on network type, attributes and id. Values are
 * set to zero or NULL if statistics are not available or applicable
 *
 * @param ip_stats_cb_token  token passed to the request function
 * @param network_type       network type
 * @param network_attrs      attributes, such as type of network_id,
 *                           security, etc.
 * @param network_id         network id
 * @param time_active        time active, if applicable
 * @param signal             signal level
 * @param station_id         base station id, e.g. WLAN access point MAC
 *                           address
 * @param dB                 raw signal strength; depends on the type of
 *                           network
 * @param rx_bytes           bytes received on the link, if applicable
 * @param tx_bytes           bytes sent on the link, if applicable
 * @param private            a reference to the icd_nw_api private member
 */
static void
icd_dbus_api_statistics_ip_cb(const gpointer ip_stats_cb_token,
                              const gchar *network_type,
                              const guint network_attrs,
                              const gchar *network_id, guint time_active,
                              guint rx_bytes, guint tx_bytes)
{
  struct icd_dbus_api_statistics_data *stats =
      (struct icd_dbus_api_statistics_data *)ip_stats_cb_token;
  struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);
  DBusMessage *msg;
  char *net_id;
  char *empty = "";

#define PVAL(v) ((v) ? &(v) : &(empty))

  if (!iap)
  {
    ILOG_WARN("dbus api ip cb stats cannot find iap %s/%0x/%s anymore, but that's ok",
              network_type, network_attrs, network_id);

    if (stats)
      icd_dbus_api_statistics_data_free(stats);

    return;
  }

  if (stats)
  {
    if (time_active)
      stats->time_active = time_active;

    if (tx_bytes || rx_bytes)
    {
      stats->rx_bytes = rx_bytes;
      stats->tx_bytes = tx_bytes;
    }

    if (iap->connection.network_id)
      net_id = iap->connection.network_id;
    else
      net_id = empty;

    msg = dbus_message_new_signal(ICD_DBUS_API_PATH,
                                  ICD_DBUS_API_INTERFACE,
                                  ICD_DBUS_API_STATISTICS_SIG);

    if (msg &&
        dbus_message_append_args(msg,
          DBUS_TYPE_STRING, PVAL(iap->connection.service_type),
          DBUS_TYPE_UINT32, &iap->connection.service_attrs,
          DBUS_TYPE_STRING, PVAL(iap->connection.service_id),
          DBUS_TYPE_STRING, PVAL(iap->connection.network_type),
          DBUS_TYPE_UINT32, &iap->connection.network_attrs,
          DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &net_id, strlen(net_id) + 1,
          DBUS_TYPE_UINT32, &stats->time_active,
          DBUS_TYPE_INT32, &stats->signal,
          DBUS_TYPE_UINT32, &stats->tx_bytes,
          DBUS_TYPE_UINT32, &stats->rx_bytes,
          DBUS_TYPE_INVALID))
    {
      if (stats->sender)
        dbus_message_set_destination(msg, stats->sender);

      icd_dbus_send_system_msg(msg);
      dbus_message_unref(msg);
    }
    else
    {
      ILOG_ERR("dbus api could not create statistics signal");

      if (msg)
        dbus_message_unref(msg);
    }

    icd_dbus_api_statistics_data_free(stats);
  }
  else
    ILOG_ERR("dbus api got NULL statistics struct in ip cb");
#undef PVAL
}

/**
 * Receive link post up statistics based on network type, attributes and id.
 * Values are set to zero or NULL if statistics are not available or
 * applicable
 *
 * @param link_post_stats_cb_token  token passed to the request function
 * @param network_type              network type
 * @param network_attrs             attributes, such as type of network_id,
 *                                  security, etc.
 * @param network_id                network id
 * @param time_active               time active, if applicable
 * @param rx_bytes                  bytes received on the link, if applicable
 * @param tx_bytes                  bytes sent on the link, if applicable
 * @param private                   a reference to the icd_nw_api private
 *                                  member
 */
static void
icd_dbus_api_statistics_link_post_cb(const gpointer link_post_stats_cb_token,
                                     const gchar *network_type,
                                     const guint network_attrs,
                                     const gchar *network_id, guint time_active,
                                     guint rx_bytes, guint tx_bytes)
{
  struct icd_dbus_api_statistics_data *stats =
      (struct icd_dbus_api_statistics_data *)link_post_stats_cb_token;
  struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);

  if (!iap)
  {
    ILOG_WARN("dbus api link post cb stats cannot find iap %s/%0x/%s anymore, but that's ok",
              network_type, network_attrs, network_id);

    if (stats)
      icd_dbus_api_statistics_data_free(stats);

    return;
  }

  if (stats)
  {
    if (time_active)
      stats->time_active = time_active;

    if (rx_bytes || tx_bytes)
    {
      stats->rx_bytes = rx_bytes;
      stats->tx_bytes = tx_bytes;
    }

    icd_iap_get_ip_stats(iap, icd_dbus_api_statistics_ip_cb, stats);
    return;
  }
  else
    ILOG_ERR("dbus api got NULL statistics struct in link post cb");
}

/**
 * Receive link statistics based on network type, attributes and id. Values
 * are set to zero or NULL if statistics are not available or applicable
 *
 * @param link_stats_cb_token  token passed to the request function
 * @param network_type         network type
 * @param network_attrs        attributes, such as type of network_id,
 *                             security, etc.
 * @param network_id           network id
 * @param time_active          time active, if applicable
 * @param signal               signal level
 * @param station_id           base station id, e.g. WLAN access point MAC
 *                             address
 * @param dB                   raw signal strength; depends on the type of
 *                             network
 * @param rx_bytes             bytes received on the link, if applicable
 * @param tx_bytes             bytes sent on the link, if applicable
 * @param private              a reference to the icd_nw_api private member
 */
static void
icd_dbus_api_statistics_link_cb(const gpointer link_stats_cb_token,
                                const gchar *network_type,
                                const guint network_attrs,
                                const gchar *network_id,
                                guint time_active, gint signal,
                                gchar *station_id, gint dB, guint rx_bytes,
                                guint tx_bytes)
{
  struct icd_dbus_api_statistics_data *stats =
      (struct icd_dbus_api_statistics_data *)link_stats_cb_token;
  struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);

  if (!iap)
  {
    ILOG_WARN("dbus api link cb stats cannot find iap %s/%0x/%s anymore, but that's ok",
              network_type, network_attrs, network_id);
    if (stats)
       icd_dbus_api_statistics_data_free(stats);

    return;
  }

  if (stats)
  {
    if (time_active)
      stats->time_active = time_active;

    if (signal)
      stats->signal = signal;

    if (station_id)
    {
      g_free(stats->station_id);
      stats->station_id = g_strdup(station_id);
    }

    if (dB)
      stats->dB = dB;

    if (rx_bytes || tx_bytes)
    {
      stats->rx_bytes = rx_bytes;
      stats->tx_bytes = tx_bytes;
    }

    icd_iap_get_link_post_stats(iap, icd_dbus_api_statistics_link_post_cb,
                                stats);
    return;
  }
  else
    ILOG_ERR("dbus api got NULL statistics struct in link cb");
}

/**
 * Function for sending statistics data to listeners
 *
 * @param iap           the IAP
 * @param foreach_data  foreach data structure
 *
 * @return  TRUE on successful signal sending, FALSE on error
 */
static gboolean
icd_dbus_api_statistics_send(struct icd_iap *iap,
                             struct icd_dbus_api_foreach_data *foreach_data)
{
  struct icd_dbus_api_statistics_data *statistics_data;

  if (!iap)
  {
    ILOG_ERR("dbus api got NULL iap in statistics request");
    return FALSE;
  }

  statistics_data = g_new0(struct icd_dbus_api_statistics_data, 1);
  statistics_data->sender = g_strdup(foreach_data->sender);
  icd_iap_get_link_stats(iap, icd_dbus_api_statistics_link_cb, statistics_data);

  return TRUE;
}

/**
 * Handle statistics requests
 *
 * @param conn       D-Bus connection
 * @param msg        D-Bus message
 * @param user_data  dbus client data
 */
static DBusHandlerResult
icd_dbus_api_statistics_req(DBusConnection *conn, DBusMessage *msg,
                            void *user_data)
{
  DBusMessage *message;
  struct icd_dbus_api_foreach_data foreach_data;

  message = dbus_message_new_method_return(msg);

  if (message)
  {
    foreach_data.connections = 0;
    foreach_data.send_fn = icd_dbus_api_statistics_send;
    foreach_data.sender = dbus_message_get_sender(msg);
    icd_dbus_api_foreach_iap_req(msg, &foreach_data);

    if (dbus_message_append_args(message,
                                  DBUS_TYPE_UINT32, &foreach_data.connections,
                                  DBUS_TYPE_INVALID))
    {
      icd_dbus_send_system_msg(message);
      dbus_message_unref(message);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    dbus_message_unref(message);

    ILOG_ERR("dbus_api could not add args to mcall return");
  }
  else
    ILOG_ERR("dbus api cannot create state req mcall return");

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * Stop iterating at the first IAP
 *
 * @param iap        the IAP struct
 * @param user_data  user data
 *
 * @return  FALSE
 */
static gboolean
icd_dbus_api_disconnect_last(struct icd_iap *iap, gpointer user_data)
{
  return FALSE;
}

/**
 * Handle disconnect requests
 *
 * @param conn       D-Bus connection
 * @param msg        D-Bus message
 * @param user_data  dbus client data
 */
static DBusHandlerResult
icd_dbus_api_disconnect_req(DBusConnection *conn, DBusMessage *msg,
                            void *user_data)
{
  guint policy_attrs;
  struct icd_iap *iap;
  DBusMessage *message;
  DBusMessageIter sub;
  DBusMessageIter iter;
  dbus_uint32_t connection_flags = 0;
  dbus_uint32_t network_attrs = 0;
  dbus_uint32_t service_attrs = 0;
  gchar *network_id = NULL;
  gchar *network_type = NULL;
  gchar *service_id = NULL;
  gchar *service_type = NULL;

  dbus_message_iter_init(msg, &iter);
  dbus_message_iter_get_basic(&iter, &connection_flags);

  if (connection_flags == ICD_CONNECTION_FLAG_NONE)
    policy_attrs = ICD_POLICY_ATTRIBUTE_BACKGROUND;
  else if (connection_flags == ICD_CONNECTION_FLAG_UI_EVENT)
    policy_attrs = ICD_POLICY_ATTRIBUTE_CONN_UI;
  else
    policy_attrs = 0;

  if (dbus_message_iter_next(&iter))
  {
    dbus_message_iter_get_basic(&iter, &service_type);
    dbus_message_iter_next(&iter);
    dbus_message_iter_get_basic(&iter, &service_attrs);
    dbus_message_iter_next(&iter);
    dbus_message_iter_get_basic(&iter, &service_id);
    dbus_message_iter_next(&iter);
    dbus_message_iter_get_basic(&iter, &network_type);
    dbus_message_iter_next(&iter);
    dbus_message_iter_get_basic(&iter, &network_attrs);
    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY ||
        dbus_message_iter_get_element_type(&iter) != DBUS_TYPE_BYTE)
    {
      ILOG_ERR("%s: dbus api wrong type for network id", "disconnect");
    }
    else
    {
      int len = 0;

      dbus_message_iter_recurse(&iter, &sub);
      dbus_message_iter_get_fixed_array(&sub, &network_id, &len);

      if (len > 0)
      {
        if (network_id[len])
        {
          network_id = (gchar *)g_realloc(network_id, len + 1);
          network_id[len] = 0;
        }
      }
    }

    dbus_message_iter_next(&iter);
    iap = icd_iap_find(network_type, network_attrs, network_id);
  }
  else
    iap = icd_iap_foreach(icd_dbus_api_disconnect_last, NULL);

  if (iap)
  {
    struct icd_request *request;
    gchar *type;

    ILOG_DEBUG("icd dbus disconnect request for %s/%0x/%s srv %s/%0x/%s",
               service_type, service_attrs, service_id, network_type,
               network_attrs, network_id);
    type = icd_gconf_get_iap_string(iap->id, "type");
    request = icd_request_find_by_iap(type, ICD_NW_ATTR_IAPNAME, iap->id);
    g_free(type);

    if (request)
      icd_request_cancel(request, policy_attrs);
    else
    {
      ILOG_INFO("icd dbus did not find request for IAP %s to disconnect",
                iap->id);
    }
  }
  else
  {
    ILOG_INFO("icd dbus did not find IAP for disconnect of %s/%0x/%s srv %s/%0x/%s",
              service_type, service_attrs, service_id, network_type,
              network_attrs, network_id);
  }

  message = dbus_message_new_method_return(msg);

  if (message)
  {
    icd_dbus_send_system_msg(message);
    dbus_message_unref(message);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  ILOG_ERR("dbus api out of memory when creating select connection reply to '%s'",
           dbus_message_get_sender(msg));

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * Handle select connection requests
 *
 * @param conn       D-Bus connection
 * @param msg        D-Bus message
 * @param user_data  dbus client data
 */
static DBusHandlerResult
icd_dbus_api_select_req(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
  guint policy_attrs;
  const char *sender;
  DBusMessage *message;
  DBusMessageIter iter;
  dbus_uint32_t connection_flags;

  dbus_message_iter_init(msg, &iter);
  dbus_message_iter_get_basic(&iter, &connection_flags);

  if (connection_flags == ICD_CONNECTION_FLAG_NONE)
    policy_attrs = ICD_POLICY_ATTRIBUTE_BACKGROUND;
  else if (connection_flags == ICD_CONNECTION_FLAG_UI_EVENT)
    policy_attrs = ICD_POLICY_ATTRIBUTE_CONN_UI;
  else
    policy_attrs = 0;

  sender = dbus_message_get_sender(msg);
  message = dbus_message_new_method_return(msg);

  if ( message )
  {
    struct icd_request *request;
    struct icd_tracking_info *track;

    icd_dbus_send_system_msg(message);
    dbus_message_unref(message);
    request =
        icd_request_new(policy_attrs, NULL, 0, NULL, NULL, 0, OSSO_IAP_ASK);
    track = icd_tracking_info_new(ICD_TRACKING_INFO_ICD2, sender, NULL);
    icd_request_tracking_info_add(request, track);
    icd_request_make(request);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  ILOG_ERR("dbus api out of memory when creating select connection reply to '%s'",
           sender);

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * Get the dbus api data structure
 * @return  the dbus api data structure
 */
static struct icd_dbus_api_listeners **
icd_dbus_api_listeners_get(void)
{
  static struct icd_dbus_api_listeners *listeners = NULL;

  if (!listeners)
     listeners = g_new0(struct icd_dbus_api_listeners, 1);

   return &listeners;
}

/**
 * Receive scan results and send them via D-Bus
 *
 * @param status        status of this network
 * @param srv_provider  service provider entry; guaranteed to exist only for
 *                      the lifetime of this callback function
 * @param cache_entry   scan results; guaranteed to exist only for the
 *                      lifetime of this callback function
 * @param user_data     D-Bus app that requested the scan
 */
static void
icd_dbus_api_scan_result(enum icd_scan_status status,
                         const struct icd_scan_srv_provider *srv_provider,
                         const struct icd_scan_cache *cache_entry,
                         gpointer user_data)
{
  DBusMessage *message;
  const gchar **service_id;
  const gchar *network_id;
  const gchar *const *service_type;
  const gchar **service_name;
  const gchar **network_type;
  const gchar **network_name;
  const gchar **station_id;
  const dbus_uint32_t *service_attrs;
  const dbus_int32_t *service_priority;
  dbus_uint32_t uzero = 0;
  dbus_int32_t izero = 0;
  const gchar *empty = "";

  message = dbus_message_new_signal(ICD_DBUS_API_PATH,
                                    ICD_DBUS_API_INTERFACE,
                                    ICD_DBUS_API_SCAN_SIG);

  if (!message)
  {
    ILOG_CRIT("dbus api out of memory when creating scan signal");
    return;
  }

  if (!dbus_message_set_destination(message, (const char *)user_data))
  {
    ILOG_CRIT("dbus api out of memory when setting destination");
    goto out;
  }

  network_id = cache_entry->network_id ? cache_entry->network_id : empty;

  if (srv_provider)
  {
    service_type = srv_provider->service_type ?
          (const gchar**)&srv_provider->service_type : &empty;
    service_name = srv_provider->service_name ?
          (const gchar**)&srv_provider->service_name : &empty;
    service_id = srv_provider->service_id ?
          (const gchar**)&srv_provider->service_id : &empty;
    service_priority = &srv_provider->service_priority;
    service_attrs = &srv_provider->service_attrs;
  }
  else
  {
    service_name = &empty;
    service_priority = &izero;
    service_attrs = &uzero;
    service_type = &empty;
    service_id = &empty;
  }

  network_type = cache_entry->network_type ?
        (const gchar**)&cache_entry->network_type : &empty;
  network_name = cache_entry->network_name ?
        (const gchar**)&cache_entry->network_name : &empty;
  station_id = cache_entry->station_id ?
        (const gchar**)&cache_entry->station_id : &empty;

  if (dbus_message_append_args(message,
                               DBUS_TYPE_UINT32,  &status,
                               DBUS_TYPE_UINT32, &cache_entry->last_seen,
                               DBUS_TYPE_STRING, service_type,
                               DBUS_TYPE_STRING, service_name,
                               DBUS_TYPE_UINT32, service_attrs,
                               DBUS_TYPE_STRING, service_id,
                               DBUS_TYPE_INT32, service_priority,
                               DBUS_TYPE_STRING, network_type,
                               DBUS_TYPE_STRING, network_name,
                               DBUS_TYPE_UINT32, &cache_entry->network_attrs,
                               DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                               &network_id, strlen(network_id) + 1,
                               DBUS_TYPE_INT32, &cache_entry->network_priority,
                               DBUS_TYPE_INT32, &cache_entry->signal,
                               DBUS_TYPE_STRING, station_id,
                               DBUS_TYPE_INT32, &cache_entry->dB,
                               DBUS_TYPE_INVALID))
  {
    icd_dbus_send_system_msg(message);
  }
  else
    ILOG_CRIT("dbus api out of memory when appending scan signal args");

out:
  dbus_message_unref(message);
}

/**
 * Append the network type of the successfully started network scan to the
 * iterator position
 *
 * @param network_type  network type to start scan for
 * @param str_iter      scan helper structure
 *
 * @return  TRUE on success, FALSE if the scan was not started
 * @todo  UI designer does not want to have the "scanning" icon blinking in
 *        this case
 */
static gboolean
icd_dbus_api_scan_append(gchar *network_type,
                         struct icd_dbus_api_scan_helper *scan_start)
{
  gboolean
      rv = icd_scan_results_request(network_type, scan_start->scan_type == 1,
                                    icd_dbus_api_scan_result,
                                    scan_start->dbus_dest);
  if (rv)
  {
    ILOG_INFO("dbus api successfully started '%s' scan for '%s'", network_type,
              scan_start->dbus_dest);

    dbus_message_iter_append_basic(scan_start->reply_str_iter,
                                   DBUS_TYPE_STRING, &network_type);
  }

  return rv;
}

/**
 * Start scan for each each given network module
 * @param module     network module
 * @param user_data  scan helper structure
 */
static gboolean
icd_dbus_api_scan_all_types(struct icd_network_module *module,
                            gpointer user_data)
{
  if (module->nw.start_search)
  {
    GSList *l;
    struct icd_dbus_api_scan_helper *helper =
        (struct icd_dbus_api_scan_helper *)user_data;

    for (l = module->network_types; l; l = l->next)
    {
      if (l->data)
        icd_dbus_api_scan_append((gchar *)l->data, helper);
    }
  }

  return TRUE;
}

/**
 * Check whether a sender already exists in the sender list
 *
 * @param sender_list  list to examine
 * @param sender       sender to look for
 *
 * @return  TRUE if found, FALSE otherwise
 */
static gboolean
icd_dbus_api_scan_sender_exists(GSList *sender_list, const gchar *sender)
{
  if (sender)
  {
    while (sender_list)
    {
      if (!strcmp((const char *)sender_list->data, sender))
        return TRUE;
      sender_list = sender_list->next;
    }
  }
  else
    ILOG_WARN("icd dbus sender list has NULL item");

  return FALSE;
}

/**
 * Handle scan requests
 *
 * @param conn       D-Bus connection
 * @param msg        D-Bus message
 * @param user_data  dbus client data
 */
static DBusHandlerResult
icd_dbus_api_scan_req(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
  const char *sender;
  DBusMessage *reply;
  gchar *dbus_dest;
  struct icd_context *icd_ctx;
  DBusMessageIter reply_str_iter;
  DBusMessageIter iter1;
  DBusMessageIter iter2;
  DBusMessageIter iter3;
  struct icd_dbus_api_scan_helper scan_helper;
  guint scan_type = 0;
  struct icd_dbus_api_listeners **listeners = icd_dbus_api_listeners_get();
  DBusMessage *message = dbus_message_new_method_return(msg);
  gboolean network_type_set = FALSE;

  if (!message)
    goto error;

  sender = dbus_message_get_sender(msg);

  if (icd_dbus_api_scan_sender_exists((*listeners)->scan_listeners, sender))
  {
    reply = dbus_message_new_error(msg, DBUS_ERROR_LIMITS_EXCEEDED,
                                   "Scan already started by you");

    if (!reply)
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    goto send_error;
  }

  dbus_message_iter_init_append(message, &iter1);

  if (!dbus_message_iter_open_container(&iter1, DBUS_TYPE_ARRAY,
                                        DBUS_TYPE_STRING_AS_STRING,
                                        &reply_str_iter))
  {
    goto error;
  }

  dbus_message_iter_init(msg, &iter3);
  dbus_message_iter_get_basic(&iter3, &scan_type);
  dbus_dest = g_strdup(dbus_message_get_sender(msg));
  (*listeners)->scan_listeners = g_slist_prepend((*listeners)->scan_listeners,
                                                 dbus_dest);
  icd_name_owner_add_filter(dbus_dest);
  scan_helper.reply_str_iter = &reply_str_iter;
  scan_helper.dbus_dest = dbus_dest;
  scan_helper.scan_type = scan_type;

  if (dbus_message_iter_next(&iter3) &&
      dbus_message_iter_get_element_type(&iter3) == DBUS_TYPE_STRING)
  {
    dbus_message_iter_recurse(&iter3, &iter2);

    while (dbus_message_iter_get_arg_type(&iter2) == DBUS_TYPE_STRING)
    {
      gchar *network_type = NULL;

      dbus_message_iter_get_basic(&iter2, &network_type);

      if (network_type &&  *network_type)
          network_type_set = TRUE;

      icd_dbus_api_scan_append(network_type, &scan_helper);
      dbus_message_iter_next(&iter2);
    }
  }

  if (!network_type_set)
  {
    icd_ctx = icd_context_get();
    icd_network_api_foreach_module(icd_ctx, icd_dbus_api_scan_all_types,
                                   &scan_helper);
  }

  dbus_message_iter_close_container(&iter1, &reply_str_iter);
  icd_dbus_send_system_msg(message);
  dbus_message_unref(message);

  return DBUS_HANDLER_RESULT_HANDLED;

error:
    reply = dbus_message_new_error(msg, DBUS_ERROR_NO_MEMORY,
                                   "Out of memory when creating reply");

    if (!reply)
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

send_error:
  icd_dbus_send_system_msg(reply);
  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;

}

/**
 * Handle connect requests
 *
 * @param conn       D-Bus connection
 * @param msg        D-Bus message
 * @param user_data  dbus client data
 *
 * @todo  OSSO_IAP_ASK merging should be done somewhere else
 */
static DBusHandlerResult
icd_dbus_api_connect_req(DBusConnection *conn, DBusMessage *msg,
                         void *user_data)
{
  DBusMessageIter iter1;
  DBusMessageIter iter2;
  DBusMessageIter iter3;
  DBusMessageIter iter4;
  guint policy_attrs;
  const char *sender;
  DBusMessage *message;
  struct icd_request *request = NULL;
  int len = 0;
  gchar *network_id = NULL;
  dbus_uint32_t network_attrs = 0;
  gchar *network_type = NULL;
  gchar *service_id = NULL;
  dbus_uint32_t service_attrs = 0;
  gchar *service_type = NULL;
  dbus_uint32_t connection_flags;

  dbus_message_iter_init(msg, &iter1);
  dbus_message_iter_get_basic(&iter1, &connection_flags);

  if (connection_flags == ICD_CONNECTION_FLAG_NONE)
  {
    policy_attrs = ICD_POLICY_ATTRIBUTE_CONN_UI |
        ICD_POLICY_ATTRIBUTE_BACKGROUND;
  }
  else if (connection_flags == ICD_CONNECTION_FLAG_UI_EVENT)
    policy_attrs = ICD_POLICY_ATTRIBUTE_CONN_UI;
  else
    policy_attrs = 0;

  sender = dbus_message_get_sender(msg);
  message = dbus_message_new_method_return(msg);

  if (!message)
  {
    ILOG_ERR("dbus api out of memory when creating connect reply to '%s'",
             sender);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  icd_dbus_send_system_msg(message);
  dbus_message_unref(message);

  if (dbus_message_iter_next(&iter1) &&
      dbus_message_iter_get_element_type(&iter1) == DBUS_TYPE_STRUCT)
  {
    dbus_message_iter_recurse(&iter1, &iter2);

    while (dbus_message_iter_get_arg_type(&iter2) == DBUS_TYPE_STRUCT)
    {
      dbus_message_iter_recurse(&iter2, &iter3);
      dbus_message_iter_get_basic(&iter3, &service_type);
      dbus_message_iter_next(&iter3);
      dbus_message_iter_get_basic(&iter3, &service_attrs);
      dbus_message_iter_next(&iter3);
      dbus_message_iter_get_basic(&iter3, &service_id);
      dbus_message_iter_next(&iter3);
      dbus_message_iter_get_basic(&iter3, &network_type);
      dbus_message_iter_next(&iter3);
      dbus_message_iter_get_basic(&iter3, &network_attrs);
      dbus_message_iter_next(&iter3);

      if (dbus_message_iter_get_arg_type(&iter3) != DBUS_TYPE_ARRAY ||
          dbus_message_iter_get_element_type(&iter3) != DBUS_TYPE_BYTE)
      {
        ILOG_ERR("%s: dbus api wrong type for network id", "connect");
        break;
      }

      dbus_message_iter_recurse(&iter3, &iter4);
      dbus_message_iter_get_fixed_array(&iter4, &network_id, &len);

      if (request || dbus_message_iter_has_next(&iter2))
      {
        if (!request)
        {
          ILOG_DEBUG("icd dbus created new OSSO_IAP_ANY request");

          request = icd_request_new(policy_attrs, NULL, 0, NULL, NULL,
                                    policy_attrs, OSSO_IAP_ANY);
        }

        ILOG_DEBUG("icd dbus add iap %s/%0x/%s srv %s/%0x/%s", service_type,
                   service_attrs, service_id, network_type, network_attrs,
                   network_id);
        icd_request_add_iap(request, service_type, service_attrs, service_id,
                            network_type, network_attrs, network_id, -1);
      }
      else
      {
        ILOG_DEBUG("icd dbus got one nw only, %s/%0x/%s srv %s/%0x/%s",
                   service_type, service_attrs, service_id, network_type,
                   network_attrs, network_id);
        request = icd_request_new(policy_attrs, service_type, service_attrs,
                                  service_id, network_type, network_attrs,
                                  network_id);
      }

      dbus_message_iter_next(&iter2);
    }
  }

  if (!request)
  {
    request =
        icd_request_new(policy_attrs, NULL, 0, NULL, NULL, 0, OSSO_IAP_ANY);
  }

  icd_request_tracking_info_add(
        request, icd_tracking_info_new(ICD_TRACKING_INFO_ICD2, sender, NULL));

  if (policy_attrs & ICD_POLICY_ATTRIBUTE_CONN_UI)
  {
    struct icd_request *merge_request = icd_request_find(NULL, 0, OSSO_IAP_ASK);

    if (merge_request)
      icd_request_merge(merge_request, request);
  }

  icd_request_make(request);

  return DBUS_HANDLER_RESULT_HANDLED;
}

/** method calls provided */
static const struct icd_dbus_mcall_table icd_dbus_api_mcalls[] = {
 {ICD_DBUS_API_SCAN_REQ, "u", "as", icd_dbus_api_scan_req},
 {ICD_DBUS_API_SCAN_REQ, "uas", "as", icd_dbus_api_scan_req},
 {ICD_DBUS_API_SCAN_CANCEL, "", "", icd_dbus_api_scan_cancel},
 {ICD_DBUS_API_CONNECT_REQ, "u", "", icd_dbus_api_connect_req},
 {ICD_DBUS_API_CONNECT_REQ, "ua(sussuay)", "", icd_dbus_api_connect_req},
 {ICD_DBUS_API_SELECT_REQ, "u", "", icd_dbus_api_select_req},
 {ICD_DBUS_API_DISCONNECT_REQ, "usussuay", "", icd_dbus_api_disconnect_req},
 {ICD_DBUS_API_DISCONNECT_REQ, "u", "", icd_dbus_api_disconnect_req},
 {ICD_DBUS_API_STATE_REQ, "sussuay", "u", icd_dbus_api_state_req},
 {ICD_DBUS_API_STATE_REQ, "", "u", icd_dbus_api_state_req},
 {ICD_DBUS_API_STATISTICS_REQ, "sussuay", "u", icd_dbus_api_statistics_req},
 {ICD_DBUS_API_STATISTICS_REQ, "", "u", icd_dbus_api_statistics_req},
 {ICD_DBUS_API_ADDRINFO_REQ, "sussuay", "u", icd_dbus_api_addrinfo_req},
 {ICD_DBUS_API_ADDRINFO_REQ, "", "u", icd_dbus_api_addrinfo_req},
 {NULL}
};

/**
 * Notify ICd2 D-Bus API when an app goes away
 * @param dbus_dest  D-Bus sender id
 * @return  TRUE if D-Bus sender was removed, FALSE otherwise
 */
gboolean
icd_dbus_api_app_exit(const gchar *dbus_dest)
{
  gboolean rv = FALSE;
  struct icd_dbus_api_listeners **listeners = icd_dbus_api_listeners_get();
  GSList *l = (*listeners)->scan_listeners;

  while (l)
  {
    gchar *listener = (gchar *)l->data;
    GSList *next = l->next;

    if (listener)
    {
      if (!strcmp(listener, dbus_dest))
      {
        ILOG_INFO("dbus api removed scanning for app '%s'", dbus_dest);

        (*listeners)->scan_listeners =
            g_slist_delete_link((*listeners)->scan_listeners, l);
        icd_scan_results_unregister(icd_dbus_api_scan_result, listener);
        icd_name_owner_remove_filter(dbus_dest);
        g_free(listener);
        rv = TRUE;
      }
    }

    l = next;
  }

  return rv;
}

/**
 * Unregister ICD2_DBUS_API
 */
void
icd_dbus_api_deinit(void)
{
  icd_dbus_unregister_system_service(ICD_DBUS_API_PATH, ICD_DBUS_API_INTERFACE);
}

/**
 * Find the handler function for the method call
 *
 * @param message  D-Bus message
 * @param table    method call table
 *
 * @return         function that handles this method call or NULL
 */
static DBusObjectPathMessageFunction
icd_dbus_api_find_handler(DBusMessage *message,
                          const struct icd_dbus_mcall_table *table)
{
  int i;
  const char *iface = dbus_message_get_interface(message);

  if (iface == NULL || strcmp(iface, ICD_DBUS_API_INTERFACE))
    return NULL;

  for (i = 0; table[i].name; i++)
  {
    if (strcmp(dbus_message_get_member(message), table[i].name) ||
        table[i].mcall_sig == NULL ||
        !dbus_message_has_signature(message, table[i].mcall_sig))
    {
      continue;
    }

    if (table[i].handler_fn)
    {
      ILOG_INFO("Received %s.%s (%s) request", iface,
                dbus_message_get_member(message),
                dbus_message_get_signature(message));

      return table[i].handler_fn;
    }
  }

  return NULL;
}

/**
 * Receive registered method calls and find a handler for them
 *
 * @param connection  D-Bus connection
 * @param message     D-Bus message
 * @param user_data   dbus api data structure
 */
static DBusHandlerResult
icd_dbus_api_request(DBusConnection *connection, DBusMessage *message,
                     void *user_data)
{
  DBusMessage *err_msg;
  DBusObjectPathMessageFunction handler_fn;

  handler_fn = icd_dbus_api_find_handler(message, icd_dbus_api_mcalls);

  if (handler_fn)
    return handler_fn(connection, message, user_data);

  ILOG_INFO("received '%s.%s' request has no handler implemented",
            dbus_message_get_interface(message),
            dbus_message_get_member(message));

  err_msg = dbus_message_new_error(message, DBUS_ERROR_NOT_SUPPORTED,
                                   "Unsupported interface or method");
  icd_dbus_send_system_msg(err_msg);
  dbus_message_unref(err_msg);

  return DBUS_HANDLER_RESULT_HANDLED;
}

/**
 * Register ICD2_DBUS_API
 * @return  TRUE on success, FALSE on failure
 */
gboolean
icd_dbus_api_init(void)
{
  icd_dbus_api_update_state(NULL, NULL, ICD_STATE_DISCONNECTED);

  return icd_dbus_register_system_service(ICD_DBUS_API_PATH,
                                          ICD_DBUS_API_INTERFACE,
                                          DBUS_NAME_FLAG_REPLACE_EXISTING |
                                          DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                          icd_dbus_api_request,
                                          NULL);
}

/**
 * Send IAP state change signal
 *
 * @param iap          the IAP or NULL if no state
 * @param destination  D-Bus destination or NULL if broadcasted to all
 * @param state        the state to send
 *
 * @return  TRUE on success, FALSE on failure
 */
gboolean
icd_dbus_api_update_state(struct icd_iap *iap, const gchar *destination,
                          const enum icd_connection_state state)
{
  DBusMessage *msg;
  const gchar *network_id;
  const gchar **service_type;
  const gchar *network_type;
  const gchar **service_id;
  const gchar **err_str;
  const gchar *empty = "";

  msg = dbus_message_new_signal(ICD_DBUS_API_PATH,
                                ICD_DBUS_API_INTERFACE,
                                ICD_DBUS_API_STATE_SIG);

  if (!msg || !dbus_message_set_destination(msg, destination))
  {
      ILOG_ERR("dbus api could not create state signal");

      if (msg)
        dbus_message_unref(msg);

      return FALSE;
  }

  if (iap)
  {
    network_id = iap->connection.network_id ?
          (const gchar *)iap->connection.network_id : empty;
    service_type = iap->connection.service_type ?
          (const gchar **)&iap->connection.service_type : &empty;
    service_id = iap->connection.service_id ?
          (const gchar **)&iap->connection.service_id : &empty;
    network_type = iap->connection.network_type ?
          (const gchar *)&iap->connection.network_type : empty;
    err_str = iap->err_str ? (const gchar **)&iap->err_str : &empty;

    if (!dbus_message_append_args(
          msg,
          DBUS_TYPE_STRING, service_type,
          DBUS_TYPE_UINT32,&iap->connection.service_attrs,
          DBUS_TYPE_STRING, service_id,
          DBUS_TYPE_STRING, network_type,
          DBUS_TYPE_UINT32, &iap->connection.network_attrs,
          DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
          &network_id, strlen(network_id) + 1,
          DBUS_TYPE_STRING, err_str,
          DBUS_TYPE_INVALID))
    {
      ILOG_ERR("dbus api could not add attributes to state signal");
      dbus_message_unref(msg);
      return FALSE;
    }
  }

  if (!dbus_message_append_args(msg,
                                DBUS_TYPE_UINT32, &state,
                                DBUS_TYPE_INVALID))
  {
    ILOG_ERR("dbus api could not add state to empty state signal");
    dbus_message_unref(msg);
    return FALSE;
  }

  icd_dbus_send_system_msg(msg);
  dbus_message_unref(msg);

  return TRUE;
}

/**
 * Get the state sent to D-Bus applications as a function of the IAP state
 *
 * @param iap  the IAP
 *
 * @return     the state to send to the D-Bus application although not yet
 *             those marked *_INTERNAL_*
 */
static enum icd_connection_state
icd_dbus_api_state_get(struct icd_iap *iap)
{
  switch (iap->state)
  {
    case ICD_IAP_STATE_SCRIPT_PRE_UP:
    case ICD_IAP_STATE_LINK_UP:
    case ICD_IAP_STATE_LINK_POST_UP:
    case ICD_IAP_STATE_IP_UP:
    case ICD_IAP_STATE_SCRIPT_POST_UP:
    case ICD_IAP_STATE_SAVING:
      return ICD_STATE_CONNECTING;
    case ICD_IAP_STATE_SRV_UP:
    {
      if (iap->limited_conn)
        return ICD_STATE_LIMITED_CONN_ENABLED;
      else
        return ICD_STATE_CONNECTING;
    }
    case ICD_IAP_STATE_CONNECTED:
      return ICD_STATE_CONNECTED;
    case ICD_IAP_STATE_CONNECTED_DOWN:
    case ICD_IAP_STATE_SRV_DOWN:
    case ICD_IAP_STATE_IP_DOWN:
    case ICD_IAP_STATE_LINK_PRE_DOWN:
    case ICD_IAP_STATE_LINK_DOWN:
    case ICD_IAP_STATE_SCRIPT_POST_DOWN:
      return ICD_STATE_DISCONNECTING;
    default:
      return ICD_STATE_DISCONNECTED;
  }
}

/**
 * Function for sending state data to listeners
 *
 * @param iap           the IAP
 * @param foreach_data  foreach data structure
 *
 * @return  TRUE on successful signal sending, FALSE on error
 */
static gboolean
icd_dbus_api_state_send(struct icd_iap *iap,
                        struct icd_dbus_api_foreach_data *foreach_data)
{
  const gchar *dest = foreach_data->sender;

  return icd_dbus_api_update_state(iap, dest, icd_dbus_api_state_get(iap));
}

/**
 * Send network scan status
 *
 * @param network_type  network type
 * @param foreach_data  foreach data structure
 *
 * @return  TRUE on successful signal sending, FALSE on error
 */
static gboolean
icd_dbus_api_state_scan_send(const gchar *network_type,
                             struct icd_dbus_api_foreach_data *foreach_data)
{
  gboolean sent = FALSE;
  const enum icd_connection_state state = ICD_STATE_SEARCH_START;
  DBusMessage *msg;

  if (network_type == NULL)
    return FALSE;

  msg = dbus_message_new_signal(ICD_DBUS_API_PATH,
                                ICD_DBUS_API_INTERFACE,
                                ICD_DBUS_API_STATE_SIG);

  if (!msg || !dbus_message_set_destination(msg, foreach_data->sender))
  {
    ILOG_ERR("dbus api could not create scan state signal");

    if (msg)
      dbus_message_unref(msg);

    return FALSE;
  }

  if (dbus_message_append_args(msg,
                               DBUS_TYPE_STRING, &network_type,
                               DBUS_TYPE_UINT32, &state,
                               DBUS_TYPE_INVALID))
  {
    sent = icd_dbus_send_system_msg(msg);

    if (sent)
      foreach_data->connections++;
  }
  else
    ILOG_ERR("dbus api could not add attributes to scan state signal");

  dbus_message_unref(msg);

  return sent;
}

/**
 * Network module callback function for scanning status
 *
 * @param module     the network module
 * @param user_data  foreach data structure
 *
 * @return  TRUE
 */
static gboolean
icd_dbus_api_state_scanning(struct icd_network_module *module,
                            gpointer user_data)
{
  struct icd_dbus_api_foreach_data *foreach_data =
      (struct icd_dbus_api_foreach_data *)user_data;
  GSList *l;

  if (!module->scan_progress)
    return TRUE;

  for ( l = module->network_types; l; l = l->next )
  {
    const gchar *network_type = (const gchar *)l->data;
    icd_dbus_api_state_scan_send(network_type, foreach_data);
  }

  return TRUE;
}

/**
 * Iterator function calling the given send function
 *
 * @param iap        the IAP
 * @param user_data  foreach data
 *
 * @return  TRUE to go through all IAPs
 */
static gboolean
icd_dbus_api_foreach_iap_all(struct icd_iap *iap, gpointer user_data)
{
  struct icd_dbus_api_foreach_data *foreach_data =
      (struct icd_dbus_api_foreach_data *)user_data;

  if (iap && foreach_data->send_fn)
  {
    if (foreach_data->send_fn(iap, user_data))
      foreach_data->connections++;
  }

  return TRUE;
}

static gboolean
icd_dbus_api_foreach_iap_req(DBusMessage *message,
                             struct icd_dbus_api_foreach_data *foreach_data)
{
  struct icd_iap *iap;
  DBusMessageIter iter;
  guint network_attrs;
  gchar *unused;
  gchar *network_id;
  gchar *network_type;

  if (dbus_message_iter_init(message, &iter))
  {
    dbus_message_iter_get_basic(&iter, &unused);
    dbus_message_iter_next(&iter);
    dbus_message_iter_get_basic(&iter, &unused);
    dbus_message_iter_next(&iter);
    dbus_message_iter_get_basic(&iter, &unused);
    dbus_message_iter_next(&iter);
    dbus_message_iter_get_basic(&iter, &network_type);
    dbus_message_iter_next(&iter);
    dbus_message_iter_get_basic(&iter, &network_attrs);
    dbus_message_iter_next(&iter);

    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY ||
        dbus_message_iter_get_element_type(&iter) != DBUS_TYPE_BYTE)
    {
      ILOG_ERR("%s: dbus api wrong type for network id", "info");
    }
    else
    {
      int len = 0;
      DBusMessageIter sub;

      dbus_message_iter_recurse(&iter, &sub);
      dbus_message_iter_get_fixed_array(&sub, &network_id, &len);

      if (len > 0 && network_id[len])
      {
        network_id = (gchar *)g_realloc(network_id, len + 1);
        network_id[len] = 0;
      }
    }

    dbus_message_iter_next(&iter);
    iap = icd_iap_find(network_type, network_attrs, network_id);

    if (iap && foreach_data->send_fn)
    {
      foreach_data->connections = 1;
      foreach_data->send_fn(iap, foreach_data);
    }
  }
  else
    icd_iap_foreach(icd_dbus_api_foreach_iap_all, foreach_data);

  return TRUE;
}

/**
 * Handle state requests
 *
 * @param conn       D-Bus connection
 * @param msg        D-Bus message
 * @param user_data  dbus client data
 */
static DBusHandlerResult
icd_dbus_api_state_req(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
  DBusMessage *reply;
  struct icd_context *icd_ctx;
  struct icd_dbus_api_foreach_data data;

  reply = dbus_message_new_method_return(msg);

  if (!reply)
  {
    ILOG_ERR("dbus api cannot create state req mcall return");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  data.connections = 0;
  data.send_fn = icd_dbus_api_state_send;
  data.sender = dbus_message_get_sender(msg);

  icd_dbus_api_foreach_iap_req(msg, &data);
  icd_ctx = icd_context_get();
  icd_network_api_foreach_module(icd_ctx, icd_dbus_api_state_scanning, &data);

  if (dbus_message_append_args(reply,
                               DBUS_TYPE_UINT32, &data.connections,
                               DBUS_TYPE_INVALID))
  {
    icd_dbus_send_system_msg(reply);
    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;
  }

  dbus_message_unref(reply);

  ILOG_ERR("dbus_api could not add args to mcall return");

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * Create and send an (N)ACK to a listener
 *
 * @param status  status
 * @param sender  D-Bus sender id
 * @param iap     IAP in question or NULL if none
 *
 * @return        TRUE if signal is sent, FALSE otherwise
 */
static
gboolean
icd_dbus_api_send_connect_sig(enum icd_connect_status status,
                              const gchar *sender, struct icd_iap *iap)
{
  guint uzero = 0;
  gchar *empty = "";
  gchar **service_type = &empty;
  gchar **service_id = &empty;
  gchar **network_type = &empty;
  gchar *network_id = empty;
  guint *service_attrs = &uzero;
  guint *network_attrs = &uzero;
  DBusMessage *msg = dbus_message_new_signal(ICD_DBUS_API_PATH,
                                             ICD_DBUS_API_INTERFACE,
                                             ICD_DBUS_API_CONNECT_SIG);
  if (!msg)
  {
    ILOG_ERR("icd dbus out of memory when creating (n)ack signal");
    return FALSE;
  }

  if (iap)
  {
    if (iap->connection.network_id)
      network_id = iap->connection.network_id;

    if (iap->connection.service_type)
      service_type = &iap->connection.service_type;

    if (iap->connection.service_id)
      service_id = &iap->connection.service_id;

    if (iap->connection.network_type)
      network_type = &iap->connection.network_type;

    network_attrs = &iap->connection.network_attrs;
    service_attrs = &iap->connection.service_attrs;
  }

  if (!dbus_message_append_args(msg,
                                DBUS_TYPE_STRING, service_type,
                                DBUS_TYPE_UINT32, service_attrs,
                                DBUS_TYPE_STRING, service_id,
                                DBUS_TYPE_STRING, network_type,
                                DBUS_TYPE_UINT32, network_attrs,
                                DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE,
                                &network_id, strlen(network_id) + 1,
                                DBUS_TYPE_UINT32, &status,
                                DBUS_TYPE_INVALID))
  {
    dbus_message_unref(msg);
    ILOG_ERR("icd dbus could not append args to (n)ack signal");
    return FALSE;
  }

  if (sender)
    dbus_message_set_destination(msg, sender);

  icd_dbus_send_system_msg(msg);
  dbus_message_unref(msg);

  return TRUE;
}

/**
 * Send a NACK on disconnect or unsuccessful connection creation to D-Bus
 * applications; free tracking info
 *
 * @param tracklist  the list of tracked applications
 * @param iap        if non-NULL send #ICD_CONNECTION_DISCONNECTED; if NULL
 *                   send #ICD_CONNECTION_NOT_CONNECTED
 */
void
icd_dbus_api_send_nack(GSList *tracklist, struct icd_iap *iap)
{
  GSList *l;

  for (l = tracklist; l; l = l->next)
  {
    struct icd_tracking_info *track = (struct icd_tracking_info *)l->data;
    enum icd_connect_status status;

    if (track && track->interface == ICD_TRACKING_INFO_ICD2)
    {
      if (iap)
        status = ICD_CONNECTION_DISCONNECTED;
      else
        status = ICD_CONNECTION_NOT_CONNECTED;

      icd_dbus_api_send_connect_sig(status, track->sender, iap);

      if (track->request)
        dbus_message_unref(track->request);

      g_free(track->sender);
      g_free(track);
      l->data = NULL;
    }
  }
}

/**
 * Send an ACK on successful connection creation to D-Bus applications
 * @param tracklist  the list of tracked applications
 * @param iap        IAP that got connected
 */
void
icd_dbus_api_send_ack(GSList *tracklist, struct icd_iap *iap)
{
  GSList *l;

  for (l = tracklist; l; l = l->next)
  {
    struct icd_tracking_info *track = (struct icd_tracking_info *)l->data;

    if (track && track->interface == ICD_TRACKING_INFO_ICD2)
    {
      icd_dbus_api_send_connect_sig(ICD_CONNECTION_SUCCESSFUL, track->sender,
                                    iap);
    }
  }
}

/**
 * Send network type search start/stop
 *
 * @param iap          the network type
 * @param destination  D-Bus destination or NULL if broadcasted to all
 * @param state        the state to send
 *
 * @return  TRUE on success, FALSE on error
 */
gboolean
icd_dbus_api_update_search(const gchar *network_type, const gchar *destination,
                           const enum icd_connection_state state)
{
  DBusMessage *msg;
  const gchar *empty = "";

  msg = dbus_message_new_signal(ICD_DBUS_API_PATH,
                                ICD_DBUS_API_INTERFACE,
                                ICD_DBUS_API_STATE_SIG);

  if (!msg || !dbus_message_set_destination(msg, destination))
  {
    ILOG_ERR("dbus api could not create state signal");

    if (msg)
      dbus_message_unref(msg);

    return FALSE;
  }

  if (dbus_message_append_args(msg,
                               DBUS_TYPE_STRING,
                               network_type ? &network_type : &empty,
                               DBUS_TYPE_UINT32, &state,
                               DBUS_TYPE_INVALID))
  {
    icd_dbus_send_system_msg(msg);
    dbus_message_unref(msg);
    return TRUE;
  }

  ILOG_ERR("dbus api could not add attributes to state signal");
  dbus_message_unref(msg);

  return FALSE;
}

/** @} */
