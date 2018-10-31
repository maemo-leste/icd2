#ifndef ICD_WLAN_DEFS
#define ICD_WLAN_DEFS

/**
@file icd_wlan_defs.h

@author patrik.flykt@nokia.com

@copyright (C) 2007 Nokia Corporation. All rights reserved.

@addtogroup icd_wlan_defs Defines for WLAN attribute bits

<dl compact>
<dt><b><a class="el" href="todo.html#_todo000018">Todo:</a></b></dt>
<dd>Remove this file when wlan provides a .h file with the needed
defines</dd>
</dl>

@ingroup internal

 * @{ */

#include <glib.h>

/** wlan infrastructure mode */
#define WLAN_TYPE_INFRA   "WLAN_INFRA"

/** wlan ad-hoc mode */
#define WLAN_TYPE_ADHOC   "WLAN_ADHOC"

/** open wlan network setting in network attributes */
#define WLAN_SECURITY_OPEN         0x1 << 3

/** wep wlan encryption in netowork attributes */
#define WLAN_SECURITY_WEP          0x2 << 3

/** wpa psk wlan encryption in network attributes */
#define WLAN_SECURITY_WPA_PSK      0x4 << 3

guint icd_wlan_defs_get_secmode (const gchar *iap_name);

gboolean icd_wlan_defs_is_wlan (const gchar *network_type);

/** @} */

#endif
