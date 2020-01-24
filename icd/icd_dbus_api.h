#ifndef ICD_DBUS_API_H
#define ICD_DBUS_API_H

#include <glib.h>
#include "dbus_api.h"
#include "icd_iap.h"

void icd_dbus_api_send_ack          (GSList *tracklist,
                                     struct icd_iap *iap);

void icd_dbus_api_send_nack         (GSList *tracklist,
                                     struct icd_iap *iap);

gboolean icd_dbus_api_update_state  (struct icd_iap *iap,
                                     const gchar *destination,
                                     const enum icd_connection_state state);

gboolean icd_dbus_api_update_search (const gchar *network_type,
                                     const gchar *destination,
                                     const enum icd_connection_state state);

gboolean icd_dbus_api_app_exit      (const gchar *dbus_dest);

gboolean icd_dbus_api_init          (void);

void icd_dbus_api_deinit            (void);


#endif
