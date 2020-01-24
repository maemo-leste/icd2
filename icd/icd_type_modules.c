/**
@file icd_type_modules.c
@copyright GNU GPLv2 or later

@addtogroup type_modules Network type to network module mapping

The network type to module name mapping is stored as a list of strings in
gconf at
\c /system/osso/connectivity/IAP/network_type/<type_name>/network_modules

@ingroup internal

 * @{ */

#include <gconf/gconf-client.h>

#include "icd_type_modules.h"
#include "icd_log.h"

/** Policy module order in gconf */
#define ICD_GCONF_POLICY_ORDER ICD_GCONF_SETTINGS "/policy/modules"

/**
 * Get the names of the network modules used for a particular network type
 *
 * @param network_type  type of network
 *
 * @return              a list of network module names that the caller has to
 *                      free or NULL if the network type is not recognised
 */
GSList *
icd_network_modules_get(const gchar *network_type)
{
  GConfClient *  gconf = gconf_client_get_default();
  GError *err = NULL;
  gchar *s = gconf_escape_key(network_type, -1);
  gchar *key = ICD_GCONF_NETWORK_TYPE_PATH(s);
  GSList *l;

  g_free(s);
  l = gconf_client_get_list(gconf, key, GCONF_VALUE_STRING, &err);
  g_free(key);

  if (err)
  {
    ILOG_WARN("error fetching type to module mapping: %s", err->message);
    g_clear_error(&err);
  }

  g_object_unref(gconf);

  return l;
}

/**
 * Get the policy module order
 *
 * @return  a list of network module names that the caller has to free or
 *          NULL on error
 */
GSList *
icd_policy_modules_get(void)
{
  GConfClient *gconf = gconf_client_get_default();
  GSList *l;
  GError *err = NULL;

  l = gconf_client_get_list(gconf, ICD_GCONF_POLICY_ORDER, GCONF_VALUE_STRING,
                            &err);

  if (err)
  {
    ILOG_WARN("error fetching policy module ordering: %s", err->message);
    g_clear_error(&err);
  }

  g_object_unref(gconf);

  return l;
}

/** @} */
