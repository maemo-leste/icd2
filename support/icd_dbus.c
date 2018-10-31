/**
@file icd_dbus.c

@authors Patrik Flykt <patrik.flykt@nokia.com>
         Aapo Mäkelä <aapo.makela@nokia.com>
         Luciano Coelho <luciano.coelho@nokia.com>

@copyright (C) 2004-2008, Nokia. All rights reserved.

@addtogroup icd_dbus D-Bus helper functions

@ingroup support_libraries

 * @{ */

#include "icd_dbus.h"
#include "icd_log.h"

struct icd_dbus_unique_name_data
{
  DBusPendingCall *pending;
  gchar *name;
  icd_dbus_get_unique_name_cb_fn cb;
  gpointer user_data;
};

/** D-Bus connection */
static DBusConnection* dbus_system_connection = NULL;
static GSList *unique_name_list = NULL;

/**
 * Get the static unique name request list
 * @return the list
 */
static GSList **
icd_dbus_get_unique_name_list(void)
{
  return &unique_name_list;
}

/** Close D-Bus system bus. */
void
icd_dbus_close()
{
  icd_dbus_cancel_unique_name(0);

  if (dbus_system_connection)
  {
    dbus_connection_unref(dbus_system_connection);
    dbus_system_connection = NULL;
  }

  dbus_shutdown();
}

/**
 * Return the connection to the system D-Bus, initialize if necessary.
 * @return the pointer to the current DBusConnection or NULL on error.
 */
static DBusConnection *
icd_dbus_get_system_bus(void)
{
  DBusError error;

  if (dbus_system_connection)
    return dbus_system_connection;

  dbus_error_init(&error);
  dbus_system_connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);

  if (dbus_error_is_set(&error))
  {
    ILOG_ERR("Failed to initialize dbus system bus: '%s'", error.message);
    dbus_error_free(&error);

    return NULL;
  }

  dbus_connection_setup_with_g_main(dbus_system_connection, NULL);

  return dbus_system_connection;
}

/**
 * Helper function for sending a D-Bus method call message used by the
 * #icd_dbus_send_message() and #icd_dbus_send_session_method_call()
 * functions. The auto activation flag will also be set in the D-Bus message.
 *
 * @param  connection  the D-Bus session/system bus connection to use
 * @param  mcall       the D-Bus method call message to send
 * @param  timeout     reply timeout in milliseconds or -1 for D-Bus default
 *                     value
 * @param  cb          callback function for the pending call, if NULL no
 *                     callback will be set
 * @param  user_data   user data to pass to the callback
 *
 * @return TRUE on success, FALSE on failure
 */
static DBusPendingCall *
icd_dbus_mcall_send(DBusConnection *connection, DBusMessage *mcall,
                    gint timeout, DBusPendingCallNotifyFunction cb,
                    void *user_data)
{
  DBusPendingCall *pending_return;

  g_return_val_if_fail(connection != NULL, NULL);
  g_return_val_if_fail(mcall != NULL, NULL);

  if (dbus_message_get_type(mcall) != DBUS_MESSAGE_TYPE_METHOD_CALL)
  {
      ILOG_ERR("dbus message %p is not a method call", mcall);
      return NULL;
  }

  if (!cb)
  {
    dbus_message_set_no_reply(mcall, TRUE);

    if (dbus_connection_send(connection, mcall, NULL))
      return pending_return;
    else
      ILOG_ERR("icd_dbus_mcall_send(): send without reply failed");

    return NULL;
  }



  if (!dbus_connection_send_with_reply(connection, mcall, &pending_return,
                                       timeout))
  {
    ILOG_ERR("icd_dbus_mcall_send(): send with reply failed");
    return NULL;
  }

  if (dbus_pending_call_set_notify(pending_return, cb, user_data, NULL))
    return pending_return;

  ILOG_ERR("icd_dbus_mcall_send(): set notify failed");

  dbus_pending_call_cancel(pending_return);

  return NULL;
}

/**
 * Function for sending a system D-Bus method call message
 *
 * @param  message    the D-Bus method call message to send
 * @param  timeout    reply timeout in milliseconds or -1 for D-Bus default
 *                    value
 * @param  cb         callback function for the pending call, if NULL no
 *                    callback will be set
 * @param  user_data  user data to pass to the callback
 *
 * @return a D-Bus pending call if a callback function is given and the
 *         method call is successfully sent, NULL otherwise; the pending call
 *         returned is referenced, the caller must unreference it after use
 */
DBusPendingCall *
icd_dbus_send_system_mcall(DBusMessage *message, gint timeout,
                           DBusPendingCallNotifyFunction cb, void *user_data)
{
  return icd_dbus_mcall_send(icd_dbus_get_system_bus(),
                             message,
                             timeout,
                             cb,
                             user_data);
}

/**
 * Helper function to send the D-Bus message (signal, error, method call
 * reply) over the given bus. Does not unref() the message.
 *
 * @param  connection  the D-Bus system or session connection
 * @param  message     the D-Bus signal to send
 *
 * @return TRUE if the message was queued for sending, FALSE otherwise
 */
static gboolean
icd_dbus_send_msg(DBusConnection *connection, DBusMessage *message)
{
  int type;

  g_return_val_if_fail(connection != NULL, FALSE);
  g_return_val_if_fail(message != NULL, FALSE);

  type = dbus_message_get_type(message);

  if (type == 	DBUS_MESSAGE_TYPE_SIGNAL ||
      type == DBUS_MESSAGE_TYPE_METHOD_RETURN ||
      type == DBUS_MESSAGE_TYPE_ERROR)
  {
    dbus_connection_send(connection, message, NULL);
    return TRUE;
  }

  ILOG_ERR("dbus message %p is not a signal, mcall return or error",
               message);

  return FALSE;
}

/**
 * Sends a D-Bus message to the system bus. Does not unref() the message.
 * @param  message  the D-Bus signal to send
 * @return TRUE if the message was a signal, method call return or error;
 *         FALSE otherwise
 */
gboolean
icd_dbus_send_system_msg(DBusMessage *message)
{
  return icd_dbus_send_msg(icd_dbus_get_system_bus(), message);
}

/**
 * Helper function for unregistering a D-Bus system bus service
 *
 * @param path     object path
 * @param service  registered service name
 */
void
icd_dbus_unregister_system_service(const char *path, const char *service)
{
  DBusConnection *dbus = icd_dbus_get_system_bus();
  DBusError error;

  dbus_error_init(&error);
  dbus_bus_release_name(dbus, service, &error);

  if (dbus_error_is_set(&error))
  {
    ILOG_ERR("Could not release service name: %s", error.message);
    dbus_error_free(&error);
  }

  if (!dbus_connection_unregister_object_path(dbus, path))
    ILOG_ERR("Could not unregister object path '%s'", path);
}

/**
 * Helper function for disconnecting a system D-Bus path
 * @param  path  path
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_dbus_disconnect_system_path (const char* path)
{
  return
      dbus_connection_unregister_object_path(icd_dbus_get_system_bus(), path);
}

/**
 * Helper function for connecting D-Bus paths to callbacks.
 *
 * @param  connection  the D-Bus bus to connect to
 * @param  path        the D-Bus signal path to connect
 * @param  cb          the callback function to call when any signal is
 *                     received
 * @param  user_data   user data passed to the callback function
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean
icd_dbus_connect_path(DBusConnection *connection, const char *path,
                      DBusObjectPathMessageFunction cb, void *user_data)
{
  DBusObjectPathVTable vt = {
    .message_function = cb,
    .unregister_function = NULL
  };

  g_return_val_if_fail(connection != NULL, FALSE);

  if (dbus_connection_register_object_path(connection, path, &vt, user_data))
    return TRUE;

  ILOG_ERR("Unable to register signal/method call path");

  return FALSE;
}

/**
 * Helper function for registering a D-Bus system bus service.
 *
 * @param  path           the D-Bus signal path to connect.
 * @param  service        the service to provide
 * @param  service_flags  D-Bus service flags
 * @param  cb             the callback function to call when any signal is
 *                        received
 * @param  user_data      user data passed to the callback function
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_dbus_register_system_service(const char *path, const char *service,
                                 guint service_flags,
                                 DBusObjectPathMessageFunction cb,
                                 void *user_data)
{
  DBusConnection *connection = icd_dbus_get_system_bus();
  DBusError error;

  g_return_val_if_fail(connection != NULL, FALSE);

  dbus_error_init(&error);

  if (icd_dbus_connect_path(connection, path, cb, user_data) &&
      dbus_bus_request_name(connection, service, service_flags, &error) != -1)
  {
    return TRUE;
  }

  ILOG_ERR("Could not register service, returned %d: %s", 0, error.message);
  dbus_error_free(&error);

  return FALSE;
}

/**
 * Helper function for connecting D-Bus system bus paths to callbacks.
 * Unregister with dbus_connection_unregister_object_path().
 *
 * @param  path       the D-Bus signal path to connect
 * @param  cb         the callback function to call when any signal is
 *                    received
 * @param  user_data  user data passed to the callback function
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_dbus_connect_system_path(const char *path, DBusObjectPathMessageFunction cb,
                             void *user_data)
{
  if (icd_dbus_connect_path(icd_dbus_get_system_bus(), path, cb, user_data))
    return TRUE;

  ILOG_ERR("Unable to register signal/method system call path");

  return FALSE;
}

/**
 * Function for disconnecting a broadcast signal callback function
 *
 * @param  interface      the interface name of the broadcasted signal
 * @param  cb             the previously added DBusHandleMessageFunction
 *                        callback
 * @param  user_data      the previously supplied user data
 * @param  extra_filters  other filters to be included in the match or NULL
 *
 * @return TRUE on successful diconnection, FALSE otherwise
 * @note   In the D-Bus documentation it is noted that it is a programming
 *         error to try to remove a callback function and user data that has
 *         not been added previously, in this case with
 *         #icd_dbus_connect_system_bcast_signal()
 */
gboolean
icd_dbus_disconnect_system_bcast_signal(const char *interface,
                                        DBusHandleMessageFunction cb,
                                        void *user_data,
                                        const char *extra_filters)
{
  gchar *rule;
  DBusError error;

  if (extra_filters)
  {
    rule = g_strdup_printf("type='signal',interface='%s',%s", interface,
                           extra_filters);
  }
  else
    rule = g_strdup_printf("type='signal',interface='%s'", interface);

  if (!rule)
    return FALSE;

  dbus_error_init(&error);
  dbus_bus_remove_match(icd_dbus_get_system_bus(), rule, &error);

  g_free(rule);

  if (dbus_error_is_set(&error))
  {
    ILOG_ERR("Could not remove match for broadcast signal: %s", error.message);
    dbus_error_free(&error);
    return FALSE;
  }

  dbus_connection_remove_filter(icd_dbus_get_system_bus(), cb, user_data);

  return TRUE;
}

/**
 * Function for connecting a callback to receive broadcast signals.
 *
 * @param  interface      the interface name of the broadcasted signal
 * @param  cb             the DBusHandleMessageFunction callback for
 *                        receiving messages
 * @param  user_data      user data to be supplied to the callback function
 * @param  extra_filters  other filters to be included in the match or NULL
 *
 * @return TRUE on success, FALSE on failure
 * @note   Since the broadcast reception is implemented as a filter for the
 *         specified interface, all sorts of D-Bus actions related to that
 *         interface will be captured. It is safe to return
 *         DBUS_HANDLER_RESULT_NOT_YET_HANDLED for unknown actions involving
 *         the callback.
 */
gboolean
icd_dbus_connect_system_bcast_signal(const char *interface,
                                     DBusHandleMessageFunction cb,
                                     void *user_data, const char *extra_filters)
{
  gchar *rule;
  DBusError error;

  if (!dbus_connection_add_filter(icd_dbus_get_system_bus(), cb, user_data,
                                  NULL))
  {
    ILOG_ERR("Could not add filter");
    return FALSE;
  }

  if (extra_filters)
  {
    rule = g_strdup_printf("type='signal',interface='%s',%s", interface,
                           extra_filters);
  }
  else
    rule = g_strdup_printf("type='signal',interface='%s'", interface);

  if (!rule)
    return FALSE;

  dbus_error_init(&error);
  dbus_bus_add_match(icd_dbus_get_system_bus(), rule, &error);
  g_free(rule);

  if (dbus_error_is_set(&error))
  {
    ILOG_ERR("Could not add match for broadcast signal: %s", error.message);
    dbus_error_free(&error);
    return FALSE;
  }

  dbus_error_free(&error);

  return TRUE;
}

/**
 * Handle 'GetNameOwner' reply
 *
 * @param pending    the pending call
 * @param user_data  callback data
 */
static void
icd_dbus_get_unique_reply(DBusPendingCall *pending, gpointer user_data)
{
  struct icd_dbus_unique_name_data *unique_name = user_data;
  DBusMessage *message;
  const gchar *s = NULL;

  message = dbus_pending_call_steal_reply(pending);

  if (message &&
      dbus_message_get_type(message) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
  {
    dbus_message_get_args(message, NULL, DBUS_TYPE_STRING, &s,
                          DBUS_TYPE_INVALID);
  }

  if (unique_name->cb)
    unique_name->cb(unique_name->name, s, unique_name->user_data);

  dbus_message_unref(message);
  unique_name->cb = NULL;
  unique_name->pending = NULL;
  icd_dbus_cancel_unique_name(pending);
}

/**
 * Get the unique D-Bus bus name for a service
 *
 * @param  name       D-Bus service name
 * @param  cb         callback
 * @param  user_data  user data
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_dbus_get_unique_name(const gchar *name, icd_dbus_get_unique_name_cb_fn cb,
                         gpointer user_data)
{
  DBusMessage *message;
  GSList **unique_name_list;

  unique_name_list = icd_dbus_get_unique_name_list();
  message = dbus_message_new_method_call(
              "org.freedesktop.DBus",
              "/org/freedesktop/DBus",
              "org.freedesktop.DBus",
              "GetNameOwner");
  if (message &&
      dbus_message_append_args(message,
                               DBUS_TYPE_STRING, &name,
                               DBUS_TYPE_INVALID))
  {
    struct icd_dbus_unique_name_data *unique_name =
        g_new0(struct icd_dbus_unique_name_data, 1);

    unique_name->name = g_strdup(name);
    unique_name->cb = cb;
    unique_name->user_data = user_data;
    unique_name->pending = icd_dbus_send_system_mcall(message, -1,
                                                      icd_dbus_get_unique_reply,
                                                      unique_name);
    *unique_name_list = g_slist_prepend(*unique_name_list, unique_name);
    dbus_message_unref(message);

    return TRUE;
  }

  ILOG_ERR("could not create 'GetNameOwner' request");
  if (message)
    dbus_message_unref(message);

  return FALSE;
}

/**
 * Cancel the unique name pending call
 * @param pending  the pending call or NULL to cancel all
 */
void
icd_dbus_cancel_unique_name(DBusPendingCall *pending)
{
  GSList **unique_list = icd_dbus_get_unique_name_list();
  GSList *l = *unique_list;


  while(l)
  {
    GSList *next = l->next;
    struct icd_dbus_unique_name_data *unique = l->data;

    if (pending && unique->pending != pending)
    {
      l = next;
      continue;
    }

    if (unique->pending)
      dbus_pending_call_cancel(unique->pending);

    if (unique->cb)
      unique->cb(unique->name, NULL, unique->user_data);

    g_free(unique->name);
    g_free(unique);

    *unique_list = g_slist_delete_link(*unique_list, l);

    if (pending)
      return;

    l = next;
  }
}

/** @} */
