#include <gconf/gconf-client.h>
#include <osso-ic-gconf.h>

#include "policy_api.h"
#include "icd_log.h"

#define ICD_POLICY_NW_DISCONNECT_GCONF ICD_GCONF_SETTINGS "/policy/policy_nw_disconnect/"

static enum icd_policy_status
icd_policy_nw_disconnect_disconnect(struct icd_policy_request *network,
                                    gint reference_count,
                                    GSList *existing_connections,
                                    gpointer *privatx)
{
  GConfClient *gconf;
  GConfValue *val;

  GError *error;

  if (!reference_count)
  {
    error = NULL;
    gconf = gconf_client_get_default();
    val = gconf_client_get(gconf,
                           ICD_POLICY_NW_DISCONNECT_GCONF "user_refcount",
                           &error);
    g_object_unref(gconf);

    if (G_VALUE_HOLDS_BOOLEAN(val) && !error)
    {
      gboolean user_refcount = gconf_value_get_bool(val);
      gconf_value_free(val);

      if (user_refcount)
      {
        ILOG_INFO("policy nw disconnect reference counting turned on, disconnecting since no apps active");
        return ICD_POLICY_ACCEPTED;
      }
    }
    else if (!error)
      ILOG_DEBUG("policy nw disconnect refcounting boolean is not set");
    else
    {
      ILOG_DEBUG("policy nw disconnect refcounting is not set, error '%s'",
                 error->message);
      g_error_free(error);
    }

    if (val)
      gconf_value_free(val);
  }

  if (reference_count >= 0)
  {
    ILOG_INFO("policy nw disconnect reference count %d, not disconnecting",
              reference_count);
    return ICD_POLICY_REJECTED;
  }

  ILOG_INFO("policy nw disconnect reference count %d, disconnect from UI",
            reference_count);

  error = NULL;
  gconf = gconf_client_get_default();
  val = gconf_client_get(gconf,
                         ICD_POLICY_NW_DISCONNECT_GCONF "cancel_always_online",
                         &error);
  g_object_unref(gconf);

  if (G_VALUE_HOLDS_BOOLEAN(val) && !error)
  {
    if (gconf_value_get_bool(val))
    {
      gconf = gconf_client_get_default();
      ILOG_DEBUG("policy nw disconnect disabling always online search interval");
      gconf_client_set_int(gconf, ICD_GCONF_NETWORK_SEARCH_INTERVAL, 0, NULL);
      g_object_unref(gconf);
    }
  }
  else if (!error)
    ILOG_DEBUG("policy nw disconnect cancel always online boolean is not set");
  else
  {
    ILOG_DEBUG("policy nw disconnect cancel always online is not set, error '%s'",
               error->message);
    g_error_free(error);
  }

  if (val)
    gconf_value_free(val);

  return ICD_POLICY_ACCEPTED;
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
  policy_api->disconnect = icd_policy_nw_disconnect_disconnect;
}
