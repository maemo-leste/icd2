#ifndef ICD_GCONF_H
#define ICD_GCONF_H

#include <glib.h>

/** The key holding the type of an IAP */
#define ICD_GCONF_IAP_TYPE "type"

/** The key holding IAP name */
#define ICD_GCONF_IAP_NAME "name"

/** The key holding temporary connection setting status */
#define ICD_GCONF_IAP_IS_TEMPORARY "temporary"

/** The key holding aggressive scanning setting status */
#define ICD_GCONF_AGGRESSIVE_SCANNING "aggressive_scanning"

gchar *icd_gconf_get_iap_string (const char *iap_name,
                                 const char *key_name);
gchar *icd_gconf_get_iap_bytearray (const char *iap_name,
                                    const char *key_name);
gboolean icd_gconf_get_iap_bool (const char *iap_name,
                                 const char *key_name,
                                 gboolean def);
gint icd_gconf_get_iap_int (const char *iap_name,
                            const char *key_name);
GSList* icd_gconf_get_iap_string_list (const char *iap_name,
                                       const char *key_name);

gboolean icd_gconf_is_temporary (const gchar *settings_name);
gboolean icd_gconf_remove_temporary (const gchar *settings_name);
gboolean icd_gconf_rename (const gchar *settings_name, const gchar *name);
gboolean icd_gconf_add_notify(void);
void icd_gconf_del_notify(void);

static inline gboolean icd_gconf_agressive_scanning()
{
        return icd_gconf_get_iap_bool(NULL,
                                      ICD_GCONF_AGGRESSIVE_SCANNING,
                                      FALSE);
}

#endif
