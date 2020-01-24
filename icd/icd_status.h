#ifndef ICD_STATUS_H
#define ICD_STATUS_H

#include <glib.h>

#include "policy_api.h"
#include "icd_iap.h"

void icd_status_limited_conn (struct icd_iap* iap,
                              const gchar *dbus_destination,
                              const gchar *err_str);

void icd_status_connect      (struct icd_iap *iap,
                              const gchar *dbus_destination,
                              const gchar *err_str);

void icd_status_connected    (struct icd_iap *iap,
                              const gchar *dbus_destination,
                              const gchar *err_str);

void icd_status_disconnect   (struct icd_iap *iap,
                              const gchar *dbus_destination,
                              const gchar *err_str);

void icd_status_disconnected (struct icd_iap *iap,
                              const gchar *dbus_destination,
                              const gchar *err_str);

void icd_status_scan_start   (const gchar *network_type);

void icd_status_scan_stop    (const gchar *network_type);

#endif
