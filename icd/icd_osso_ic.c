/**
@file icd_osso_ic.c

@authors timo.teras@nokia.com
         patrik.flykt@nokia.com

@copyright (C) 2004-2007 Nokia Corporation. All rights reserved.

@addtogroup icd_osso_ic Compatibility functions providing OSSO IC D-Bus API

@ingroup internal

 * @{ */

#include <string.h>
#include <dbus/dbus.h>
#include <gconf/gconf.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <osso-ic-ui-dbus.h>
#include <osso-ic.h>
#include <osso-ic-dbus.h>
#include <icd_osso_ic.h>
#include "icd_dbus.h"
#include "icd_log.h"
#include "icd_gconf.h"
#include "network_api.h"
#include "icd_iap.h"
#include "icd_request.h"
#include "icd_tracking_info.h"
#include "icd_status.h"
#include "icd_wlan_defs.h"

/** milliseconds to wait for UI to respond to requests; used only for log
 * message printing for now */
#define ICD_OSSO_UI_REQUEST_TIMEOUT   4 * 1000

/** ip data structure */
struct icd_osso_ic_ipdata {
  /** how many times the callback has been called */
  guint has_called;

  /** how many times the callback will be called */
  guint howmany;

  /** D-Bus request */
  DBusMessage *request;
};

/** connection statistics */
struct icd_osso_ic_stats_data {
  /** method call for the statistics request */
  DBusMessage *request;

  /** time active */
  guint time_active;

  /** signal strength */
  enum icd_nw_levels signal;

  /** base station id */
  gchar *station_id;

  /** raw signal strength */
  gint dB;

  /** received packets */
  guint rx_packets;

  /** sent packets */
  guint tx_packets;

  /** received bytes */
  guint rx_bytes;

  /** sent bytes */
  guint tx_bytes;
};

/** Callback data passed for UI method calls */
struct icd_osso_ic_mcall_data {
  /** pending call, if needed */
  DBusPendingCall *pending_call;

  /** method call name */
  gchar *mcall_name;

  /** callback */
  icd_osso_ui_cb_fn cb;

  /** user_data */
  gpointer user_data;
};

/** Function that handles an incoming OSSO IC API request
 * @param  request    the D-Bus message
 * @param  user_data  user data
 * @return the D-Bus reply or NULL if a reply is sent later
 */
typedef DBusMessage*(* icd_osso_ic_message_handler)(DBusMessage *request, void *user_data);

/** Structure containing information to match a D-Bus message with the
 * correct handler function */
struct icd_osso_ic_handler {
  /** D-Bus interface */
  const char *interface;

  /** D-Bus method */
  const char *method;

  /** D-Bus message signature */
  const char *signature;

  /** function handling this method call */
  icd_osso_ic_message_handler handler;
};

struct icd_osso_ic_get_state_data
{
  const char *sender;
  guint connections;
};

/**
 * Helper function for fetching the IAP type from gconf
 * @param  iap_name  name of the IAP
 * @return IAP type that is to be freed by the caller
 */
static gchar *
icd_osso_ic_get_type(const gchar *iap_name)
{
  return icd_gconf_get_iap_string(iap_name, ICD_GCONF_IAP_TYPE);
}

/**
 * Make a new request
 *
 * @param  merge_request  request to merge with this one or NULL
 * @param  track          new tracking info or NULL
 * @param  message        the D-Bus request message to send errors to
 * @param  requested_iap  the requested IAP name
 * @param  flags          ICD_POLICY_ATTRIBUTE_* flags to add to the request
 *
 * @return a D-Bus error on failure, NULL on success
 */
static DBusMessage *
icd_osso_ic_make_request(struct icd_request *merge_request,
                         struct icd_tracking_info *track, DBusMessage *message,
                         const gchar *requested_iap, const guint flags)
{
  guint network_attrs = ICD_NW_ATTR_IAPNAME;
  struct icd_request *request;
  gchar *network_type = NULL;

  if (strcmp(OSSO_IAP_ANY, requested_iap) &&
      strcmp(OSSO_IAP_ASK, requested_iap))
  {
    network_type = icd_osso_ic_get_type(requested_iap);

    if (!network_type)
    {
      ILOG_ERR("network type cannot be NULL for requested iap '%s'", requested_iap);
      return dbus_message_new_error(message, ICD_DBUS_ERROR_INVALID_IAP,
                                    "IAP type not found in gconf");
    }

    if (icd_wlan_defs_is_wlan(network_type))
      network_attrs |= icd_wlan_defs_get_secmode(requested_iap);
  }

  request = icd_request_new(flags, NULL, 0, NULL, network_type, network_attrs,
                            requested_iap);

  if (merge_request)
    icd_request_merge(merge_request, request);

  if (track)
    icd_request_tracking_info_add(request, track);

  icd_request_make(request);
  g_free(network_type);

  return NULL;
}

/**
 * IAP activation via connectivity libraries.
 *
 * @param  method_call  the D-Bus request message
 * @param  user_data    user data
 *
 * @return a D-Bus reply to the request
 */
static DBusMessage *
icd_osso_ic_connect(DBusMessage *method_call, void *user_data)
{
  DBusMessage *msg;

  if (dbus_message_get_type(method_call) == DBUS_MESSAGE_TYPE_METHOD_CALL)
  {
    struct icd_tracking_info *track;
    DBusError error;
    guint flags;
    gchar *iap;

    dbus_error_init(&error);
    dbus_message_get_args(method_call, &error,
                          DBUS_TYPE_STRING, &iap,
                          DBUS_TYPE_UINT32, &flags,
                          DBUS_TYPE_INVALID);
    dbus_error_free(&error);

    if (flags & OSSO_IAP_TIMED_CONNECT)
    {
      flags = ICD_POLICY_ATTRIBUTE_BACKGROUND |
          ICD_POLICY_ATTRIBUTE_NO_INTERACTION;
    }
    else
      flags = 0;

    track = icd_tracking_info_new(ICD_TRACKING_INFO_ICD,
                                  dbus_message_get_sender(method_call),
                                  method_call);

    msg = icd_osso_ic_make_request(NULL, track, method_call, iap, flags);
  }
  else
  {
    ILOG_ERR("message to 'connect' is not a method call");
    msg = dbus_message_new_error(method_call, DBUS_ERROR_NOT_SUPPORTED,
                                 "Message is not a method call");
  }

  return msg;
}

/**
 * Desktop background killing application
 *
 * @param  method_call  the D-Bus request message
 * @param  user_data    user data
 *
 * @return a D-Bus reply to the request
 */
static DBusMessage *
icd_osso_ic_bg_killed(DBusMessage *method_call, void *user_data)
{
  gchar *s;
  struct icd_tracking_info *track;
  const gchar *sender;
  const gchar *application;

  dbus_message_get_args(method_call, NULL,
                        DBUS_TYPE_STRING, &application,
                        DBUS_TYPE_STRING, &sender,
                        DBUS_TYPE_INVALID);

  s = g_strdup_printf("com.nokia.%s", application);
  track = icd_tracking_info_find(sender);

  if (track)
  {
    ILOG_INFO("application '%s' ('%s') background killed", application, sender);

    icd_tracking_info_update(track, s, NULL);
  }
  else
  {
    ILOG_DEBUG("application '%s' ('%s') background killed but not tracked",
               application, sender);
  }

  g_free(s);

  return dbus_message_new_method_return(method_call);
}

/**
 * Report state for all connections
 *
 * @param  request    request to examine
 * @param  user_data  get state data structure
 *
 * @return NULL
 */
static gpointer
icd_osso_ic_get_state_foreach(struct icd_request *request, gpointer user_data)
{
  struct icd_osso_ic_get_state_data *state_data;
  struct icd_iap *iap;

  state_data = (struct icd_osso_ic_get_state_data *)user_data;

  if (request->try_iaps)
  {
    ILOG_DEBUG("querying connection state for request %p", request);

    iap = (struct icd_iap *)request->try_iaps->data;

    if (iap)
    {
      switch ( iap->state )
      {
        case ICD_IAP_STATE_SCRIPT_PRE_UP:
        case ICD_IAP_STATE_LINK_UP:
        case ICD_IAP_STATE_LINK_POST_UP:
        case ICD_IAP_STATE_IP_UP:
        case ICD_IAP_STATE_SCRIPT_POST_UP:
        case ICD_IAP_STATE_SAVING:
          icd_status_connect(iap, state_data->sender, NULL);
          state_data->connections++;
          return NULL;
        case ICD_IAP_STATE_SRV_UP:
          if (iap->limited_conn)
            icd_status_limited_conn(iap, state_data->sender, NULL);
          else
            icd_status_connect(iap, state_data->sender, NULL);
          break;
        case ICD_IAP_STATE_CONNECTED:
          icd_status_connected(iap, state_data->sender, NULL);
          break;
        case ICD_IAP_STATE_CONNECTED_DOWN:
        case ICD_IAP_STATE_SRV_DOWN:
        case ICD_IAP_STATE_IP_DOWN:
        case ICD_IAP_STATE_LINK_PRE_DOWN:
        case ICD_IAP_STATE_LINK_DOWN:
        case ICD_IAP_STATE_SCRIPT_POST_DOWN:
          icd_status_disconnect(iap, state_data->sender, NULL);
          break;
        default:
          return NULL;
      }

      state_data->connections++;
    }
    else
      ILOG_CRIT("request %p, NULL iap", request);
  }
  else
    ILOG_DEBUG("request %p does not have any IAPs", request);

  return NULL;
}

/**
 * Get state for all connections
 *
 * @param  method_call  the D-Bus request message
 * @param  user_data    user data
 *
 * @return an D-Bus reply to the request
 */
static DBusMessage *
icd_osso_ic_get_state(DBusMessage *method_call, void *user_data)
{
  struct icd_osso_ic_get_state_data *state_data;
  DBusMessage *msg;

  state_data = g_new0(struct icd_osso_ic_get_state_data, 1);
  state_data->sender = dbus_message_get_sender(method_call);
  icd_request_foreach(icd_osso_ic_get_state_foreach, state_data);

  ILOG_INFO("connection state for %d connections sent",
            state_data->connections);

  msg = dbus_message_new_method_return(method_call);
  dbus_message_append_args(msg,
                           DBUS_TYPE_UINT32, &state_data->connections,
                           DBUS_TYPE_INVALID);
  g_free(state_data);

  return msg;
}

/**
 * Find the first connected request which has an iap that can give us ip
 * address information
 *
 * @param  request    request to examine
 * @param  user_data  ip data structure
 *
 * @return iap which returned ip data, NULL otherwise
 */
static gpointer
icd_osso_ic_ipinfo_get_first(struct icd_request *request, gpointer user_data)
{
  if (request->state != ICD_REQUEST_SUCCEEDED || !request->try_iaps)
  {
    ILOG_DEBUG("request %p not in ICD_REQUEST_SUCCEEDED state", request);
    return NULL;
  }

  ILOG_DEBUG("querying ip info from request %p", request);

  if (request->try_iaps->data)
    return request->try_iaps->data;

  ILOG_CRIT("request %p in ICD_REQUEST_SUCCEEDED state but NULL iap", request);

  return NULL;
}

/**
 * Send connection statistics error
 * @param method_call  the method call to reply to
 */
static void
icd_osso_ic_connstats_error(DBusMessage *method_call)
{
  DBusMessage *msg = dbus_message_new_error(method_call,
                                            ICD_DBUS_ERROR_IAP_NOT_AVAILABLE,
                                            "IAP does not exist anymore");

  icd_dbus_send_system_msg(msg);
  dbus_message_unref(msg);
}
/**
 * Receive IP address configuration information based on network type,
 * attributes and id.
 *
 * @param  addr_info_cb_token  token passed to the request function
 * @param  network_type        network type
 * @param  network_attrs       attributes, such as type of network_id,
 *                             security, etc.
 * @param  network_id          IAP name or local id, e.g. SSID
 * @param  private             a reference to the icd_nw_api private member
 * @param  ip_address          IP address string or NULL if no such value
 * @param  ip_netmask          IP netmask string which or NULL if no such
 *                             value
 * @param  ip_gateway          IP gateway string which or NULL if no such
 *                             value
 * @param  ip_dns1             DNS server IP address string or NULL if no
 *                             such value
 * @param  ip_dns2             DNS server IP address string or NULL if no
 *                             such value
 * @param  ip_dns3             DNS server IP address string or NULL if no
 *                             such value
 *
 * @return TRUE if some of the values are returned, FALSE if no values
 *         assigned
 */
static void
icd_osso_ic_ipinfo_cb(gpointer addr_info_cb_token, const gchar *network_type,
                      const guint network_attrs, const gchar *network_id,
                      gchar *ip_address, gchar *ip_netmask, gchar *ip_gateway,
                      gchar *ip_dns1, gchar *ip_dns2, gchar *ip_dns3)
{
  struct icd_osso_ic_ipdata *data =
      (struct icd_osso_ic_ipdata *)addr_info_cb_token;

  if (!data)
  {
    ILOG_ERR("ip addr info returned NULL ipdata");
    return;
  }

  if (!data->request)
  {
    ILOG_DEBUG("ip info called %d time(s), reply already sent, but that's ok",
               data->has_called + 1);
  }
  else
  {
    struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);

    if (!iap)
    {
      ILOG_WARN("ip stats cannot find iap %s/%0x/%s anymore, but that's ok",
                network_type, network_attrs, network_id);
      icd_osso_ic_connstats_error(data->request);
    }
    else
    {
      DBusMessage *msg;

      if (!ip_address)
        ip_address = "";

      if (!ip_netmask)
        ip_netmask = "";

      if (!ip_gateway)
        ip_gateway = "";

      if (!ip_dns1)
        ip_dns1 = "";

      if (!ip_dns2)
        ip_dns2 = "";

      msg = dbus_message_new_method_return(data->request);

      if (msg)
      {
        if (dbus_message_append_args(msg,
                                     DBUS_TYPE_STRING, &network_id,
                                     DBUS_TYPE_STRING, &ip_address,
                                     DBUS_TYPE_STRING, &ip_netmask,
                                     DBUS_TYPE_STRING, &ip_gateway,
                                     DBUS_TYPE_STRING, &ip_dns1,
                                     DBUS_TYPE_STRING, &ip_dns2,
                                     DBUS_TYPE_INVALID))
        {
          ILOG_DEBUG("Returning IP info %s/%s %s %s/%s for iap %p", ip_address,
                     ip_netmask, ip_gateway, ip_dns1, ip_dns2, iap);
        }
        else
        {
          dbus_message_unref(msg);
          msg = NULL;
        }
      }

      if (!msg)
      {
        msg = dbus_message_new_error(
              data->request, DBUS_ERROR_NO_MEMORY,
              "Could not create get_ipinfo method call reply");
      }

      if (msg)
      {
        icd_dbus_send_system_msg(msg);
        dbus_message_unref(msg);
      }
      else
        ILOG_CRIT("Could not create get_ipinfo method call error reply");
    }

    ILOG_DEBUG("ip data called for the first time (%d)", data->has_called + 1);
    dbus_message_unref(data->request);
    data->request = NULL;
  }

  data->has_called++;

  if (data->has_called == data->howmany)
  {
    ILOG_DEBUG("ip info deleted ipdata %p in callback", data);
    g_free(data);
  }
}

/**
 * Get IPv4 address info for the latest connection
 *
 * @param  method_call  the D-Bus request message
 * @param  user_data    user data
 *
 * @return an D-Bus reply to the request
 */
static DBusMessage *
icd_osso_ic_ipinfo(DBusMessage *method_call, void *user_data)
{
  struct icd_osso_ic_ipdata *data = g_new0(struct icd_osso_ic_ipdata, 1);
  struct icd_iap *iap =
      (struct icd_iap *)icd_request_foreach(icd_osso_ic_ipinfo_get_first, data);

  if (!iap)
  {
    ILOG_INFO("no ipv4 info available");
    g_free(data);
    return dbus_message_new_error(method_call,
                                  ICD_DBUS_ERROR_IAP_NOT_AVAILABLE,
                                  "No active IAP");
  }

  dbus_message_ref(method_call);
  data->request = method_call;

  ILOG_DEBUG("requesting ip info from %p", iap);

  data->howmany = icd_iap_get_ipinfo(iap, icd_osso_ic_ipinfo_cb, data);
  ILOG_DEBUG("ipinfo says %d cbs expected, has called %d", data->howmany,
             data->has_called);

  if (data->howmany == data->has_called)
  {
    /*
      fmg - something's fishy there or I am too stupid to get it:
      the same condition exists at the end of icd_osso_ic_ipinfo_cb, I hope it
      would not lead to double-free of data struct
     */
    ILOG_DEBUG("ip info deleted ipdata %p", data);

    if (data->request)
    {
      dbus_message_unref(data->request);
      data->request = NULL;
    }

    g_free(data);
  }

  return NULL;
}

/**
 * Disconnection requested by application
 *
 * @param  method_call  the D-Bus request message
 * @param  user_data    user data
 *
 * @return an D-Bus reply to the request
 */
static DBusMessage *
icd_osso_ic_disconnect(DBusMessage *method_call, void *user_data)
{
  gchar *network_type;
  struct icd_request *request;
  DBusMessage *message;
  gchar *iap_name;

  dbus_message_get_args(method_call, NULL,
                        DBUS_TYPE_STRING, &iap_name,
                        DBUS_TYPE_INVALID);

  network_type = icd_osso_ic_get_type(iap_name);
  request = icd_request_find_by_iap(network_type,  ICD_NW_ATTR_IAPNAME,
                                    iap_name);

  if (request)
  {
    const char *sender;
    struct icd_tracking_info *track;

    g_free(network_type);
    sender = dbus_message_get_sender(method_call);
    track = icd_tracking_info_find(sender);

    if (track)
    {
      if (!icd_tracking_info_update(track, NULL, method_call))
      {
        icd_request_tracking_info_add(
              request,
              icd_tracking_info_new(ICD_TRACKING_INFO_ICD,
                                    dbus_message_get_sender(method_call),
                                    method_call));
      }

      icd_request_cancel(request, 0);
      message = NULL;
    }
    else
    {
      ILOG_ERR("dbus user '%s' has not requested '%s'",
               dbus_message_get_sender(method_call), iap_name);

      message = dbus_message_new_error(method_call,
                                       ICD_DBUS_ERROR_INVALID_IAP,
                                       "You have not connected to that IAP");
    }
  }
  else
  {
    ILOG_ERR("no such IAP '%s' with type '%s'", iap_name, network_type);
    g_free(network_type);
    message = dbus_message_new_error(method_call,
                                     ICD_DBUS_ERROR_INVALID_IAP,
                                     "IAP not found in gconf");
  }

  return message;
}

/**
 * IAP activation requested by the 'Select connection' UI dialog or
 * Connection Manager, i.e. a connection request from connectivity components
 * without any reference counting
 *
 * @param  method_call  the D-Bus request message
 * @param  user_data    user data
 *
 * @return a D-Bus reply to the request
 * @todo   OSSO_IAP_ASK merging should be done somewhere else
 */
static DBusMessage *
icd_osso_ic_activate(DBusMessage *method_call, void *user_data)
{
  if (dbus_message_get_type(method_call) == DBUS_MESSAGE_TYPE_METHOD_CALL)
  {
    DBusMessage *message;
    gchar *requested_iap;
    DBusError error;
    struct icd_request *request;

    dbus_error_init(&error);
    dbus_message_get_args(method_call, &error,
                          DBUS_TYPE_STRING, &requested_iap,
                          DBUS_TYPE_INVALID);
    dbus_error_free(&error);
    request = icd_request_find(NULL, 0, OSSO_IAP_ASK);

    if (*requested_iap)
    {
      message = icd_osso_ic_make_request(request, NULL, method_call,
                                         requested_iap,
                                         ICD_POLICY_ATTRIBUTE_CONN_UI);

      if (message)
        return message;
    }
    else
    {
      ILOG_DEBUG("cancel pressed in UI");

      if (request)
      {
        icd_request_send_nack(request);
        icd_request_cancel(request, ICD_POLICY_ATTRIBUTE_CONN_UI);
      }
    }

    return dbus_message_new_method_return(method_call);
  }

  ILOG_ERR("message to 'activate' is not a method call");

  return dbus_message_new_error(method_call,
                                DBUS_ERROR_NOT_SUPPORTED,
                                "Message is not a method call");
}

/**
 * Generic pending call callback that prints out whether the UI D-Bus request
 * succeeded or not
 *
 * @param pending    the pending call
 * @param user_data  the requested UI D-Bus method call
 */
static void
icd_osso_ic_ui_pending(DBusPendingCall *pending, void *user_data)
{
  struct icd_osso_ic_mcall_data *data =
      (struct icd_osso_ic_mcall_data *)user_data;
  DBusMessage *reply;
  gboolean success = FALSE;

  reply = dbus_pending_call_steal_reply(pending);

  if (data->pending_call)
  {
    dbus_pending_call_unref(data->pending_call);
    data->pending_call = NULL;
  }

  if (dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_ERROR )
    success = TRUE;

  if (success)
    ILOG_DEBUG("'%s' successfully requested from UI", data->mcall_name);
  else
  {
    ILOG_DEBUG("'%s' requested from UI but returned: '%s'", data->mcall_name,
               dbus_message_get_error_name(reply));
  }

  if (data->cb)
  {
    ILOG_DEBUG("icd UI callback %p called with success '%d' user data %p",
               data->cb, success, data->user_data);
    data->cb(success, data->user_data);
  }
  else
    ILOG_DEBUG("icd UI callback NULL");

  g_free(data);
}

/**
 * Connection disconnection requested by Connectivity UIs; request disconnect
 * confirmation
 *
 * @param  request    the D-Bus request message
 * @param  user_data  user data
 *
 * @return an D-Bus reply to the request
 */
static DBusMessage *
icd_osso_ic_shutdown(DBusMessage *request, void *user_data)
{
  DBusMessage *mcall;
  DBusMessage *message;
  DBusError error;

  dbus_error_init(&error);
  mcall = dbus_message_new_method_call(ICD_UI_DBUS_SERVICE,
                                       ICD_UI_DBUS_PATH,
                                       ICD_UI_DBUS_INTERFACE,
                                       ICD_UI_SHOW_DISCONNDLG_REQ);
  if (mcall)
  {
    struct icd_osso_ic_mcall_data *data =
        g_new0(struct icd_osso_ic_mcall_data, 1);

    data->mcall_name = ICD_UI_SHOW_DISCONNDLG_REQ;
    data->pending_call = icd_dbus_send_system_mcall(mcall,
                                                    ICD_OSSO_UI_REQUEST_TIMEOUT,
                                                    icd_osso_ic_ui_pending,
                                                    data);

    if (!data->pending_call)
    {
      ILOG_WARN("icd UI '%s' mcall could not be sent, user has to retry pressing disconnect",
                data->mcall_name);
      g_free(data);
    }

    dbus_message_unref(mcall);
    message = dbus_message_new_method_return(request);
  }
  else
  {
    dbus_error_free(&error);
    ILOG_ERR("Could not create show_disconnect_dlg method call");
    message = dbus_message_new_error(
                request,
                "org.freedesktop.DBus.Error.NoMemory",
                "Could not create show_disconnect_dlg method call");
  }
  return message;
}

/**
 * Find the first connected request that has an iap which can return
 * statistics
 *
 * @param  request    request to examine
 * @param  user_data  connection statistics data structure
 *
 * @return iap which returned ip statistics, NULL otherwise
 */
static gpointer
icd_osso_ic_connstats_get_first(struct icd_request *request, gpointer user_data)
{
  struct icd_iap *iap = NULL;

  if (request->state == ICD_REQUEST_SUCCEEDED && request->try_iaps)
  {
    ILOG_DEBUG("querying statistics from request %p", request);
    iap = (struct icd_iap *)request->try_iaps->data;

    if (!iap)
    {
      ILOG_CRIT("request %p in ICD_REQUEST_SUCCEEDED state but NULL iap",
                request);
    }
  }
  else
    ILOG_DEBUG("request %p not in ICD_REQUEST_SUCCEEDED state", request);

  return iap;
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
icd_osso_ic_connstats_ip_cb(const gpointer ip_stats_cb_token,
                            const gchar *network_type,
                            const guint network_attrs, const gchar *network_id,
                            guint time_active, guint rx_bytes, guint tx_bytes)
{
  struct icd_osso_ic_stats_data *stats =
      (struct icd_osso_ic_stats_data *)ip_stats_cb_token;
  struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);
  DBusMessage *message;
  dbus_uint32_t zero = 0;

  if (iap)
  {
    if (stats)
    {
      if (time_active)
        stats->time_active = time_active;

      if (tx_bytes || rx_bytes)
      {
        stats->rx_bytes = rx_bytes;
        stats->tx_bytes = tx_bytes;
      }

      message = dbus_message_new_method_return(stats->request);

      if (message &&
          dbus_message_append_args(message,
                                   DBUS_TYPE_STRING, &network_id,
                                   DBUS_TYPE_UINT32, &stats->time_active,
                                   DBUS_TYPE_UINT32, &stats->dB,
                                   DBUS_TYPE_UINT32, &zero,
                                   DBUS_TYPE_UINT32, &zero,
                                   DBUS_TYPE_UINT32, &stats->rx_bytes,
                                   DBUS_TYPE_UINT32, &stats->tx_bytes,
                                   DBUS_TYPE_INVALID))
      {
        ILOG_DEBUG("returning statistics for iap %p: %u, %u, %u, %u, %u, %u",
                   iap, stats->time_active, stats->signal, zero,  zero,
                   stats->rx_bytes, stats->tx_bytes);
      }
      else
      {
        if (message)
          dbus_message_unref(message);

        message = dbus_message_new_error(
              stats->request, DBUS_ERROR_NO_MEMORY,
              "Could not create get_statistics method call reply");
      }

      if (message)
      {
        icd_dbus_send_system_msg(message);
        dbus_message_unref(message);
      }

      dbus_message_unref(stats->request);
      g_free(stats->station_id);
      g_free(stats);
    }
  }
  else if (stats)
  {
    icd_osso_ic_connstats_error(stats->request);
    dbus_message_unref(stats->request);
    g_free(stats->station_id);
    g_free(stats);
  }
  else
    ILOG_ERR("ip stats is NULL");

  if (!iap)
  {
    ILOG_WARN("ip stats cannot find iap %s/%0x/%s anymore, but that's ok",
              network_type, network_attrs, network_id);
  }
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
 * @param signal                    signal level
 * @param station_id                base station id, e.g. WLAN access point
 *                                  MAC address
 * @param dB                        raw signal strength; depends on the type
 *                                  of network
 * @param rx_bytes                  bytes received on the link, if applicable
 * @param tx_bytes                  bytes sent on the link, if applicable
 * @param private                   a reference to the icd_nw_api private
 *                                  member
 */
static void
icd_osso_ic_connstats_link_post_cb(const gpointer link_post_stats_cb_token,
                                   const gchar *network_type,
                                   const guint network_attrs,
                                   const gchar *network_id, guint time_active,
                                   guint rx_bytes, guint tx_bytes)
{
  struct icd_osso_ic_stats_data *stats =
      (struct icd_osso_ic_stats_data *)link_post_stats_cb_token;
  struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);

  if (iap)
  {
    if (stats)
    {
      if (time_active)
        stats->time_active = time_active;

      if (tx_bytes || rx_bytes)
      {
        stats->rx_bytes = rx_bytes;
        stats->tx_bytes = tx_bytes;
      }

      icd_iap_get_ip_stats(iap, icd_osso_ic_connstats_ip_cb, stats);
    }
  }
  else if (stats)
  {
    icd_osso_ic_connstats_error(stats->request);
    dbus_message_unref(stats->request);
    g_free(stats->station_id);
    g_free(stats);
  }
  else
    ILOG_ERR("link post stats is NULL");

  if (!iap)
  {
    ILOG_WARN("link post stats cannot find iap %s/%0x/%s anymore, but that's ok",
              network_type, network_attrs, network_id);
  }
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
icd_osso_ic_connstats_link_cb(gpointer link_stats_cb_token,
                              const gchar *network_type,
                              const guint network_attrs,
                              const gchar *network_id, guint time_active,
                              gint signal, gchar *station_id, gint dB,
                              guint rx_bytes, guint tx_bytes)
{
  struct icd_osso_ic_stats_data *stats =
      (struct icd_osso_ic_stats_data *)link_stats_cb_token;
  struct icd_iap *iap = icd_iap_find(network_type, network_attrs, network_id);

  if (iap)
  {
    if (stats)
    {
      stats->time_active = time_active;
      stats->signal = signal;
      stats->dB = dB;
      stats->rx_bytes = rx_bytes;
      stats->tx_bytes = tx_bytes;
      stats->station_id = g_strdup(station_id);;
      icd_iap_get_link_post_stats(iap, icd_osso_ic_connstats_link_post_cb,
                                  stats);
    }
  }
  else if (stats)
  {
    icd_osso_ic_connstats_error(stats->request);
    dbus_message_unref(stats->request);
    g_free(stats->station_id);
    g_free(stats);
  }
  else
    ILOG_ERR("link stats is NULL");

  if (!iap)
  {
    ILOG_WARN("link stats cannot find iap %s/%0x/%s anymore, but that's ok",
              network_type, network_attrs, network_id);
  }
}

/**
 * Get connection statistics info for the latest connection
 *
 * @param  method_call  the D-Bus request message
 * @param  user_data    user data
 *
 * @return an D-Bus reply to the request
 */
static DBusMessage *
icd_osso_ic_connstats(DBusMessage *method_call, void *user_data)
{
  struct icd_iap *iap;
  gchar *network_id;

  if (!dbus_message_get_args(method_call, NULL,
                             DBUS_TYPE_STRING, &network_id,
                             DBUS_TYPE_INVALID))
  {
    network_id = NULL;
  }

  if (!network_id)
  {
    ILOG_DEBUG("no arguments, finding any/first iap");
    iap = (struct icd_iap *)icd_request_foreach(icd_osso_ic_connstats_get_first,
                                                NULL);
  }
  else
  {
    gchar *network_type = icd_osso_ic_get_type(network_id);
    iap = icd_iap_find(network_type, ICD_NW_ATTR_IAPNAME, network_id);
    g_free(network_type);
  }

  if (iap)
  {
    struct icd_osso_ic_stats_data *stats_data =
        g_new0(struct icd_osso_ic_stats_data, 1);

    dbus_message_ref(method_call);
    stats_data->request = method_call;
    icd_iap_get_link_stats(iap, icd_osso_ic_connstats_link_cb, stats_data);
  }
  else
  {
    ILOG_INFO("no connection statistics available");
    icd_osso_ic_connstats_error(method_call);
  }

  return NULL;
}

/** OSSO IC API method call handlers */
static struct icd_osso_ic_handler icd_osso_ic_htable[] = {
  {ICD_DBUS_INTERFACE, ICD_ACTIVATE_REQ, "s", icd_osso_ic_activate},
  {ICD_DBUS_INTERFACE, ICD_SHUTDOWN_REQ, "", icd_osso_ic_shutdown},
  {ICD_DBUS_INTERFACE, ICD_CONNECT_REQ, "su", icd_osso_ic_connect},
  {ICD_DBUS_INTERFACE, ICD_DISCONNECT_REQ, "s", icd_osso_ic_disconnect},
  {ICD_DBUS_INTERFACE, ICD_GET_IPINFO_REQ, "", icd_osso_ic_ipinfo},
  {ICD_DBUS_INTERFACE, ICD_GET_STATISTICS_REQ, "", icd_osso_ic_connstats},
  {ICD_DBUS_INTERFACE, ICD_GET_STATISTICS_REQ, "s", icd_osso_ic_connstats},
  {ICD_DBUS_INTERFACE, ICD_GET_STATE_REQ, "", icd_osso_ic_get_state},
  {ICD_DBUS_INTERFACE, "background_killing_application", "ss",
   icd_osso_ic_bg_killed},
  {NULL}
};

/**
 * Disconnect received from UI; disconnect the last request as this way we
 * can cancel a new connecting connection
 *
 * @param  signal     the D-Bus request message
 * @param  user_data  not used
 *
 * @return NULL, as there received signal does not need a reply
 */
static DBusMessage *
icd_osso_ui_disconnect(DBusMessage *signal, void *user_data)
{
  DBusError error;
  dbus_bool_t disconnect_pressed;

  dbus_error_init(&error);
  dbus_message_get_args(signal, &error,
                        DBUS_TYPE_BOOLEAN, &disconnect_pressed,
                        DBUS_TYPE_INVALID);

  dbus_error_free(&error);

  if (disconnect_pressed)
  {
    GSList *request_list = icd_context_get()->request_list;
    struct icd_request *request;

    if (request_list)
      request = (struct icd_request *)request_list->data;
    else
      request = NULL;

    if (request)
    {
      ILOG_INFO("disconnect selected for '%s.%s', disconnecting request %p",
                dbus_message_get_interface(signal),
                dbus_message_get_member(signal), request);

      icd_request_cancel(request, ICD_POLICY_ATTRIBUTE_CONN_UI);
    }
    else
    {
      ILOG_WARN("disconnect selected for '%s.%s', but no requests",
                dbus_message_get_interface(signal),
                dbus_message_get_member(signal));
    }
  }
  else
  {
    ILOG_INFO("cancel selected for '%s.%s'", dbus_message_get_interface(signal),
              dbus_message_get_member(signal));
  }

  return NULL;
}

/**
 * Save connection received from the UI
 *
 * @param  signal     the D-Bus request message
 * @param  user_data  not used
 *
 * @return NULL, as there received signal does not need a reply
 */
static DBusMessage *
icd_osso_ui_save(DBusMessage *signal, void *user_data)
{
  gchar *network_type;
  struct icd_iap *iap;
  DBusError error;
  gchar *name;
  gchar *iap_name;

  dbus_error_init(&error);
  dbus_message_get_args(signal, &error,
                        DBUS_TYPE_STRING, &iap_name,
                        DBUS_TYPE_STRING, &name,
                        DBUS_TYPE_INVALID);
  dbus_error_free(&error);
  network_type = icd_osso_ic_get_type(iap_name);
  iap = icd_iap_find(network_type, ICD_NW_ATTR_IAPNAME, iap_name);

  if (iap)
  {
    if (icd_iap_rename(iap, name))
      ILOG_DEBUG("Saved '%s' as '%s'", iap_name, name);
    else
      ILOG_WARN("IAP '%s' was not renamed", iap_name);
  }
  else
    ILOG_WARN("IAP '%s' not found when save signal received", iap_name);

  g_free(network_type);

  return NULL;
}

/**
 * Retry signal received from UI
 *
 * @param  signal     the D-Bus request message
 * @param  user_data  not used
 *
 * @return NULL, as there received signal does not need a reply
 */
static DBusMessage *
icd_osso_ui_retry(DBusMessage *signal, void *user_data)
{
  gchar *network_type = NULL;
  DBusMessageIter iter;
  dbus_bool_t retry_this = FALSE;
  dbus_bool_t retry = FALSE;
  const gchar *id = NULL;
  struct icd_request *request;
  struct icd_request *new_request;

  dbus_message_iter_init(signal, &iter);
  dbus_message_iter_get_basic(&iter, &id);
  dbus_message_iter_next(&iter);
  dbus_message_iter_get_basic(&iter, &retry);

  if (dbus_message_iter_next(&iter))
    dbus_message_iter_get_basic(&iter, &retry_this);

  if (!strcmp(id, OSSO_IAP_ANY) || !strcmp(id, OSSO_IAP_ASK) )
  {
    ILOG_DEBUG("searching for meta iap '%s'", id);
    request = icd_request_find(NULL, 0, id);
  }
  else
  {
    ILOG_DEBUG("searching for normal iap '%s'", id);
    network_type = icd_osso_ic_get_type(id);

    if (network_type)
      request = icd_request_find_by_iap(network_type, ICD_NW_ATTR_IAPNAME, id);
    else
      request = icd_request_find_by_iap_id(id, TRUE);
  }

  ILOG_DEBUG("retry from UI: id '%s', retry %d, retry_this %d", id, retry,
             retry_this);

  if (retry)
  {
    if (request && request->state == ICD_REQUEST_WAITING)
    {
      GSList *try_iaps;

      if (retry_this)
      {
        new_request = request;
        goto make_req;
      }

      try_iaps = request->try_iaps;

      if (try_iaps)
      {
        struct icd_iap *iap =
            (struct icd_iap *)g_slist_nth_data(try_iaps, retry_this);

        if (iap)
        {
          ILOG_DEBUG("retry from UI: disconnected from %p", iap);
          icd_status_disconnected(iap, NULL, NULL);
        }
      }

      icd_request_free_iaps(request);
      new_request = icd_request_new(0, NULL, 0, NULL, NULL, 0, OSSO_IAP_ASK);
    }
    else
    {
      ILOG_WARN("cannot find request %p in state ICD_REQUEST_WAITING for iap '%s', type '%s'",
                request, id, network_type);
      new_request = icd_request_new(0, NULL, 0, NULL, NULL, 0, OSSO_IAP_ASK);
    }

    if (request)
      icd_request_merge(request, new_request);

make_req:
    icd_request_make(new_request);
  }
  else
  {
    ILOG_DEBUG("cancel signalled from retry UI");

    if (request)
    {
      icd_request_send_nack(request);
      icd_request_cancel(request, ICD_POLICY_ATTRIBUTE_CONN_UI);
    }
  }

  g_free(network_type);

  return NULL;
}

/** UI signal handlers */
static struct icd_osso_ic_handler icd_osso_ui_htable[] = {
  {ICD_UI_DBUS_INTERFACE, ICD_UI_DISCONNECT_SIG, "b", icd_osso_ui_disconnect},
  {ICD_UI_DBUS_INTERFACE, ICD_UI_RETRY_SIG, "sb", icd_osso_ui_retry},
  {ICD_UI_DBUS_INTERFACE, ICD_UI_RETRY_SIG, "sbb", icd_osso_ui_retry},
  {ICD_UI_DBUS_INTERFACE, ICD_UI_SAVE_SIG, "ss", icd_osso_ui_save},
  {NULL}
};

/**
 * Search a handler function for given D-BUS message.
 *
 * @param  msg       D-BUS message that needs handler.
 * @param  handlers  Pointer to list of handler functions and method
 *                   descriptions.
 *
 * @return Pointer to handler function or NULL on error.
 */
static icd_osso_ic_message_handler
icd_osso_ic_find_handler(DBusMessage *msg, struct icd_osso_ic_handler *handlers)
{
  const char *iface = dbus_message_get_interface(msg);
  const char *member = dbus_message_get_member(msg);

  while (handlers->interface)
  {
    if (!strcmp(iface, handlers->interface) &&
        !strcmp(member, handlers->method) &&
        (!handlers->signature ||
         dbus_message_has_signature(msg, handlers->signature)))
    {
      return handlers->handler;
    }

    handlers++;
  }

  return NULL;
}

/**
 * Callback function for OSSO IC API requests
 *
 * @param  connection  the D-Bus connection
 * @param  message     the D-Bus message
 * @param  user_data   user data
 *
 * @return DBUS_HANDLER_RESULT_HANDLED
 */
static DBusHandlerResult
icd_osso_ic_request(DBusConnection *connection, DBusMessage *message,
                    void *user_data)
{
  icd_osso_ic_message_handler handler;
  DBusMessage *msg;

  handler = icd_osso_ic_find_handler(message, icd_osso_ic_htable);

  if (handler)
  {
    ILOG_INFO("Received %s.%s (%s) request",
              dbus_message_get_interface(message),
              dbus_message_get_member(message),
              dbus_message_get_signature(message));

    msg = handler(message, user_data);
  }
  else
  {
    ILOG_INFO("received '%s.%s' request is not recognized",
              dbus_message_get_interface(message),
              dbus_message_get_member(message));

    msg = dbus_message_new_error(message, DBUS_ERROR_NOT_SUPPORTED,
                                 "Unsupported interface or method");
  }

  if (msg)
  {
    icd_dbus_send_system_msg(msg);
    dbus_message_unref(msg);
  }

  return DBUS_HANDLER_RESULT_HANDLED;
}

/**
 * UI signal handler
 *
 * @param connection  The D-Bus connection
 * @param message     D-Bus message
 * @param user_data   user data
 */
static DBusHandlerResult
icd_osso_ui_signal(DBusConnection *connection, DBusMessage *message,
                   void *user_data)
{
  if (dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_SIGNAL)
  {
    if (!strcmp(ICD_UI_DBUS_INTERFACE, dbus_message_get_interface(message)))
    {
      icd_osso_ic_message_handler handler =
          icd_osso_ic_find_handler(message, icd_osso_ui_htable);

      if (handler)
      {
        ILOG_INFO("received '%s.%s' (%s) signal from UI",
                  dbus_message_get_interface(message),
                  dbus_message_get_member(message),
                  dbus_message_get_signature(message));

        handler(message, user_data);
      }
      else
      {
        ILOG_ERR("received '%s.%s' (%s) request is not recognized",
                 dbus_message_get_interface(message),
                 dbus_message_get_member(message),
                 dbus_message_get_signature(message));
      }
    }
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/**
 * Initialize the OSSO IC API
 * @param  icd_ctx  icd context
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_osso_ic_init(struct icd_context *icd_ctx)
{
  if (!icd_dbus_register_system_service(ICD_DBUS_PATH,
                                        ICD_DBUS_SERVICE,
                                        DBUS_NAME_FLAG_REPLACE_EXISTING |
                                        DBUS_NAME_FLAG_DO_NOT_QUEUE,
                                        icd_osso_ic_request,
                                        NULL))
  {
    ILOG_CRIT("could not register '" ICD_DBUS_SERVICE "'");
    return FALSE;
  }

  ILOG_DEBUG("listening on " ICD_DBUS_SERVICE);

  if (!icd_dbus_connect_system_bcast_signal(ICD_UI_DBUS_INTERFACE,
                                            icd_osso_ui_signal, NULL, NULL))
  {
    ILOG_CRIT("could not connect to '" ICD_UI_DBUS_INTERFACE "'");

    icd_dbus_unregister_system_service(ICD_DBUS_PATH,
                                       ICD_DBUS_SERVICE);
    return FALSE;
  }

  ILOG_DEBUG("listening for " ICD_UI_DBUS_INTERFACE " messages ");

  return TRUE;
}

/**
 * Deinitialize OSSO IC API
 * @param icd_ctx  icd context
 */
void
icd_osso_ic_deinit(void)
{
  icd_dbus_unregister_system_service(ICD_DBUS_PATH, ICD_DBUS_SERVICE);
  icd_dbus_disconnect_system_bcast_signal(ICD_UI_DBUS_INTERFACE,
                                          icd_osso_ui_signal, NULL, NULL);
}

/**
 * Cancel save dialog pending call
 * @param send_save_token  the opaque token returned from
 *                         icd_osso_ui_send_save()
 */
void
icd_osso_ui_send_save_cancel(gpointer send_save_token)
{
  DBusPendingCall **pending = (DBusPendingCall **)send_save_token;

  if (pending && *pending)
  {
    dbus_pending_call_cancel(*pending);
    g_free(pending);
  }
}

/**
 * Send nacks for connect and disconnect requests; free tracking info
 * @param tracking_list  list of applications to track
 */
void
icd_osso_ic_send_nack(GSList *tracking_list)
{
  GSList *l;

  for (l = tracking_list; l; l = l->next)
  {
    struct icd_tracking_info *track = (struct icd_tracking_info *)l->data;

    if (track->interface == ICD_TRACKING_INFO_ICD )
    {
      if (track->request)
      {
        DBusMessage *msg =
            dbus_message_new_error(track->request,
                                   ICD_DBUS_ERROR_IAP_NOT_AVAILABLE,
                                   "Failed to establish requested IAP");

        if (msg)
        {
          ILOG_INFO("Sending nack to '%s'", track->sender);

          icd_dbus_send_system_msg(msg);
          dbus_message_unref(msg);
        }
        else
        {
          ILOG_CRIT("failed to create nack for '%s' for request %p",
                    track->sender, track->request);
        }

        dbus_message_unref(track->request);
        track->request = NULL;
      }

      g_free(track->sender);
      g_free(track);
      l->data = NULL;
    }
  }
}

/**
 * Request UI to show a retry dialog
 *
 * @param iap_name   name of the IAP to retry
 * @param error      ICD_DBUS_ERROR_* error string
 * @param cb         callback function
 * @param user_data  callback user data
 */
void
icd_osso_ui_send_retry(const gchar *iap_name, const gchar *error,
                       icd_osso_ui_cb_fn cb, gpointer user_data)
{
  struct icd_osso_ic_mcall_data *mcall_data;
  DBusMessage *mcall;
  DBusPendingCall *pending;

  mcall_data = g_new0(struct icd_osso_ic_mcall_data, 1);
  mcall_data->user_data = user_data;
  mcall_data->cb = cb;
  mcall_data->mcall_name = ICD_UI_SHOW_RETRY_REQ;
  mcall = dbus_message_new_method_call(ICD_UI_DBUS_SERVICE,
                                       ICD_UI_DBUS_PATH,
                                       ICD_UI_DBUS_INTERFACE,
                                       ICD_UI_SHOW_RETRY_REQ);
  if (mcall)
  {
    if (dbus_message_append_args(mcall,
                                 DBUS_TYPE_STRING, &iap_name,
                                 DBUS_TYPE_STRING, &error,
                                 DBUS_TYPE_INVALID))
    {
      pending = icd_dbus_send_system_mcall(mcall, ICD_OSSO_UI_REQUEST_TIMEOUT,
                                           icd_osso_ic_ui_pending, mcall_data);
      mcall_data->pending_call = pending;

      if (pending)
      {
        dbus_message_unref(mcall);
        return;
      }

      ILOG_WARN("icd UI mcall '%s' could not be sent", mcall_data->mcall_name);
    }
    else
      ILOG_ERR("could not append args to '%s' request", mcall_data->mcall_name);

    dbus_message_unref(mcall);
  }
  else
    ILOG_ERR("could not create '%s' request", mcall_data->mcall_name);

  if (mcall_data->cb)
    mcall_data->cb(FALSE, mcall_data->user_data);

  g_free(mcall_data);
}

/**
 * Send acks for connect and disconnect requests
 *
 * @param tracking_list  list of applications to track
 * @param iap_name       iap name to return to the application
 */
void
icd_osso_ic_send_ack(GSList *tracking_list, const gchar *iap_name)
{
  GSList *l;

  if (!iap_name)
  {
    ILOG_CRIT("no iap name when sending ack");
    return;
  }

  for (l = tracking_list; l; l = l->next)
  {
    struct icd_tracking_info *track = (struct icd_tracking_info *)l->data;
    DBusMessage *msg;

    if (!track->request || track->interface)
      continue;

    msg = dbus_message_new_method_return(track->request);

    if (msg)
    {
      if (dbus_message_append_args(msg, DBUS_TYPE_STRING, &iap_name, 0))
      {
        ILOG_INFO("Sending ack for iap '%s' to '%s'", iap_name, track->sender);

        icd_dbus_send_system_msg(msg);
        dbus_message_unref(track->request);
        track->request = NULL;
      }
      else
      {
        ILOG_CRIT("failed to append arg to ack for '%s' request %p",
                  track->sender, track->request);
      }

      dbus_message_unref(msg);
    }
    else
    {
      ILOG_CRIT("failed to create ack for '%s' for request %p", track->sender,
                track->request);
    }
  }
}

/**
 * Request the UI to show a save dialog
 *
 * @param  iap_name   name of the IAP to save
 * @param  cb         callback function
 * @param  user_data  callback user data
 *
 * @return opaque token that can be used to cancel the save dialog request
 */
gpointer
icd_osso_ui_send_save(const gchar *iap_name, icd_osso_ui_cb_fn cb,
                      gpointer user_data)
{
  struct icd_osso_ic_mcall_data *mcall_data;
  DBusMessage *message;
  DBusPendingCall *pending_call;

  mcall_data = g_new0(struct icd_osso_ic_mcall_data, 1);
  mcall_data->cb = cb;
  mcall_data->user_data = user_data;
  mcall_data->mcall_name = ICD_UI_SHOW_SAVEDLG_REQ;

  message = dbus_message_new_method_call(ICD_UI_DBUS_SERVICE,
                                         ICD_UI_DBUS_PATH,
                                         ICD_UI_DBUS_INTERFACE,
                                         ICD_UI_SHOW_SAVEDLG_REQ);
  if (message)
  {
    if (dbus_message_append_args(message,
                                 DBUS_TYPE_STRING, &iap_name,
                                 DBUS_TYPE_INVALID))
    {
      pending_call = icd_dbus_send_system_mcall(message,
                                                ICD_OSSO_UI_REQUEST_TIMEOUT,
                                                icd_osso_ic_ui_pending,
                                                mcall_data);
      mcall_data->pending_call = pending_call;

      if (pending_call)
      {
        dbus_message_unref(message);
        return mcall_data;
      }

      ILOG_WARN("icd UI mcall '%s' could not be sent", mcall_data->mcall_name);
    }
    else
      ILOG_ERR("could not append args to '%s' request", mcall_data->mcall_name);

    dbus_message_unref(message);
  }
  else
    ILOG_ERR("could not create '%s' request", mcall_data->mcall_name);

  if (mcall_data->cb)
    mcall_data->cb(FALSE, mcall_data->user_data);

  g_free(mcall_data);

  return NULL;
}

/** @} */
