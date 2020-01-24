/**
@file icd_network_priority.c
@copyright GNU GPLv2 or later

@addtogroup icd_network_prio Network priority assignment
@ingroup internal

 * @{ */

#include <string.h>
#include <gconf/gconf-client.h>
#include <osso-ic-gconf.h>
#include "icd_log.h"
#include "icd_network_priority.h"
#include "network_api.h"

/** preferred service network priority */
#define ICD_NW_PRIO_SRV_PREF   500

/** WLAN network prefix */
#define ICD_NW_TYPE_WLAN   "WLAN_"

/** WLAN priority */
#define ICD_NW_PRIO_WLAN   60

/** WiMAX network */
#define ICD_NW_TYPE_WIMAX   "WIMAX"

/** WiMax priority */
#define ICD_NW_PRIO_WIMAX   50

/** GPRS network */
#define ICD_NW_TYPE_GPRS   "GPRS"

/** GPRS priority */
#define ICD_NW_PRIO_GPRS   45

/** GSM GPRS network */
#define ICD_NW_TYPE_DUN_GSM_PS   "DUN_GSM_PS"

/** CDMA packet data network */
#define ICD_NW_TYPE_DUN_CDMA_PSD   "DUN_CDMA_PSD"

/** GSM/CDMA packet data priority */
#define ICD_NW_PRIO_DUN_PS   40

/** GSM circuit switched network */
#define ICD_NW_TYPE_DUN_GSM_CS   "DUN_GSM_CS"

/** CDMA circuit switched network */
#define ICD_NW_TYPE_DUN_CDMA_CSD   "DUN_CDMA_CSD"

/** CDMA Quick Net connect network */
#define ICD_NW_TYPE_DUN_CDMA_QNC   "DUN_CDMA_QNC"

/** GSM/CDMA circuit switched priority */
#define ICD_NW_PRIO_DUN_CS   30

/** What is the highest normal (not preferred) priority */
#define ICD_NW_PRIO_HIGHEST   ICD_NW_PRIO_WLAN

/** The saved IAP priority is made higher */
#define ICD_NW_PRIO_SAVED_BOOSTER_VALUE   100

/** preferred service id */
#define PREFERRED_SERVICE_ID ICD_GCONF_SETTINGS "/srv_provider/preferred_id"

/** preferred service type */
#define PREFERRED_SERVICE_TYPE ICD_GCONF_SETTINGS "/srv_provider/preferred_type"

/** preferred service id */
static gchar *preferred_id = NULL;

/** preferred service type */
static gchar *preferred_type = NULL;

/**
 * (Re)set preferred service type and id
 */
void
icd_network_priority_pref_init (void)
{
  GConfClient *gconf = gconf_client_get_default();

  g_free(preferred_id);
  preferred_id = gconf_client_get_string(gconf, PREFERRED_SERVICE_ID, NULL);

  g_free(preferred_type);
  preferred_type = gconf_client_get_string(gconf,
                                           PREFERRED_SERVICE_TYPE, NULL);

  ILOG_DEBUG("preferred srv type '%s', srv id '%s'", preferred_type,
             preferred_id);
}

/**
 * Set static network priority for the cahce entry
 *
 * @param srv_type       service type or NULL if none
 * @param srv_id         service id or NULL if none
 * @param network_type   network type
 * @param network_attrs  network attrs
 *
 * @return  the network priority
 */
gint
icd_network_priority_get(const gchar *srv_type, const gchar *srv_id,
                         const gchar *network_type, const guint network_attrs)
{
  guint priority = 0;

  if (preferred_id && preferred_type && srv_type && srv_id &&
      !strcmp(preferred_type, srv_type) && !strcmp(preferred_id, srv_id))
  {
    return ICD_NW_PRIO_SRV_PREF;
  }

  if (network_attrs & ICD_NW_ATTR_IAPNAME)
    priority = ICD_NW_PRIO_SAVED_BOOSTER_VALUE;

  if (!strncmp(network_type, ICD_NW_TYPE_WLAN, strlen(ICD_NW_TYPE_WLAN)))
    priority += ICD_NW_PRIO_WLAN;
  else if (!strcmp(network_type, ICD_NW_TYPE_WIMAX))
    priority += ICD_NW_PRIO_WIMAX;
  else if (!strcmp(network_type, ICD_NW_TYPE_DUN_GSM_PS) ||
           !strcmp(network_type, ICD_NW_TYPE_DUN_CDMA_PSD))
  {
    priority += ICD_NW_PRIO_DUN_PS;
  }
  else if (!strcmp(network_type, ICD_NW_TYPE_DUN_GSM_CS) ||
           !strcmp(network_type, ICD_NW_TYPE_DUN_CDMA_CSD))
  {
    priority += ICD_NW_PRIO_DUN_CS;
  }
  else if (!strcmp(network_type, ICD_NW_TYPE_DUN_CDMA_QNC))
    priority += ICD_NW_PRIO_DUN_CS;

  return priority;
}

/**
 * Check if there are more higher priority networks available. Return also
 * the priority if the return pointer is set. This function can be called
 * from policy modules (policy_always_online.so calls this func)
 *
 * @param srv_type       service type or NULL if none
 * @param srv_id         service id or NULL if none
 * @param network_type   network type
 * @param network_attrs  network attrs
 * @param the            network priority (returned to caller)
 *
 * @return  TRUE if found higher priority network types, FALSE if not found
 */
gboolean
icd_network_priority(const gchar *srv_type, const gchar *srv_id,
                     const gchar *network_type, const guint network_attrs,
                     gint *network_priority)
{
  gint priority;

  if (network_priority)
    *network_priority = icd_network_priority_get(srv_type, srv_id, network_type,
                                                 network_attrs);

  if (preferred_id && preferred_type && srv_type && srv_id)
  {
    if (!strcmp(preferred_type, srv_type))
      return strcmp(preferred_id, srv_id) != 0;
    else
      return TRUE;
  }

  if (network_priority)
    priority = *network_priority;
  else
  {
    priority = icd_network_priority_get(NULL, NULL, network_type,
                                        network_attrs);
  }

  if (network_attrs & ICD_NW_ATTR_IAPNAME)
    priority -= ICD_NW_PRIO_SAVED_BOOSTER_VALUE;

  return priority < ICD_NW_PRIO_WLAN;
}

/** @} */
