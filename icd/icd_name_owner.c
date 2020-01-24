/**
@file icd_name_owner.c
@copyright GNU GPLv2 or later

@addtogroup icd_name_owner D-Bus NameOwnerChanged message handling
@ingroup internal

 * @{ */

#include <dbus/dbus.h>
#include <osso-ic-ui-dbus.h>
#include <osso-ic.h>

#include <string.h>

#include "icd_log.h"
#include "icd_dbus.h"
#include "icd_request.h"
#include "icd_name_owner.h"
#include "icd_tracking_info.h"
#include "icd_dbus_api.h"

#define ICD_NAME_OWNER_FILTER_STRING   "member='NameOwnerChanged',arg0='%s'"

/**
 * D-Bus filter function for NameOwnerChanged messages
 *
 * @param connection  D-Bus system bus
 * @param message     D-Bus message
 * @param user_data   icd context
 *
 * @return  DBUS_HANDLER_RESULT_NOT_YET_HANDLED if error,
 *          DBUS_HANDLER_RESULT_HANDLED otherwise
 */
static DBusHandlerResult
icd_name_owner_filter(DBusConnection *connection, DBusMessage *message,
                      void *user_data)
{
  const gchar *new;
  const gchar *old;
  const gchar *name;

  if (!dbus_message_is_signal(message, DBUS_INTERFACE_DBUS, "NameOwnerChanged"))
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

  if (!dbus_message_get_args(message, NULL,
                             DBUS_TYPE_STRING, &name,
                             DBUS_TYPE_STRING, &old,
                             DBUS_TYPE_STRING, &new,
                             DBUS_TYPE_INVALID))
  {
    ILOG_WARN("Invalid arguments for NameOwnerChanged signal");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (*new)
  {
    if (*old)
      return DBUS_HANDLER_RESULT_HANDLED;

    if (!strcmp(ICD_UI_DBUS_SERVICE, name))
      ILOG_WARN("connectivity UI service '" ICD_UI_DBUS_SERVICE "' started");
    else
    {
      struct icd_tracking_info *track = icd_tracking_info_find(name);

      if (track)
      {
        ILOG_INFO("application '%s' ('%s') restored", name, new);

        icd_tracking_info_update(track, new, NULL);
      }
    }
  }
  else
  {
    if (*name != ':')
    {
      if (!strcmp("com.nokia.icd_ui", name))
      {
        struct icd_request *request;

        ILOG_WARN("connectivity UI service '"ICD_UI_DBUS_SERVICE"' exited");

        request = icd_request_find(NULL, 0, OSSO_IAP_ASK);

        if (request)
          icd_request_cancel(request, ICD_POLICY_ATTRIBUTE_CONN_UI);
      }
    }
    else if (icd_request_tracking_info_delete(name) ||
             icd_dbus_api_app_exit(name))
    {
        ILOG_INFO("tracked application '%s' ('%s') exited", name, old);
    }
  }

  return DBUS_HANDLER_RESULT_HANDLED;
}

/**
 * Add a filter for NameOwnerChanged signals for a specific application
 * @param application  application D-Bus id
 * @return  the return status of icd_dbus_connect_system_bcast_signal
 */
gboolean
icd_name_owner_add_filter(const gchar *application)
{
  gchar *filter;
  gboolean rv;

  filter = g_strdup_printf(ICD_NAME_OWNER_FILTER_STRING, application);
  rv = icd_dbus_connect_system_bcast_signal(DBUS_INTERFACE_DBUS,
                                            icd_name_owner_filter, NULL,
                                            filter);
  g_free(filter);

  return rv;
}

/**
 * Remove a filter for NameOwnerChanged signals for a specific application
 * @param application  application D-Bus id
 * @return  the return status of icd_dbus_disconnect_system_bcast_signal()
 */
gboolean
icd_name_owner_remove_filter(const gchar *application)
{
  gchar *filter;
  gboolean rv;

  filter = g_strdup_printf(ICD_NAME_OWNER_FILTER_STRING, application);
  rv = icd_dbus_disconnect_system_bcast_signal(DBUS_INTERFACE_DBUS,
                                               icd_name_owner_filter, NULL,
                                               filter);
  g_free(filter);

  return rv;
}

/**
 * Initialize NameOwnerChanged filtering
 * @param icd_ctx  icd context
 * @return  TRUE on success, FALSE on failure
 */
gboolean
icd_name_owner_init(struct icd_context *icd_ctx)
{
  return icd_name_owner_add_filter(ICD_UI_DBUS_SERVICE);
}

/** @} */
