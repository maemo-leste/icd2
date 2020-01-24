/**
@file icd_wlan_defs.c
@copyright GNU GPLv2 or later

@addtogroup icd_wlan_defs Defines for WLAN attribute bits
@ingroup internal

 * @{ */

#include <string.h>
#include "icd_wlan_defs.h"
#include "icd_gconf.h"

/**
 * Get wlan security attributes for an IAP
 * @param iap_name  IAP name
 * @return  wlan security network attributes
 */
guint
icd_wlan_defs_get_secmode(const gchar *iap_name)
{
  gchar *s;
  guint rv = 0;

  s = icd_gconf_get_iap_string(iap_name, "wlan_security");

  if (!s)
    return 0;

  if (!strcmp(s, "WEP"))
    rv = WLAN_SECURITY_WEP;

  g_free(s);

  return rv;
}

/**
 * Check whether the network type is wlan
 * @param network_type  network type
 * @return  TRUE if wlan, FALSE otherwise
 */
gboolean
icd_wlan_defs_is_wlan(const gchar *network_type)
{
  if ( network_type )
  {
    if (!strcmp(network_type, WLAN_TYPE_INFRA) ||
        !strcmp(network_type, WLAN_TYPE_ADHOC))
    {
      return TRUE;
    }
  }

  return FALSE;
}

/** @} */
