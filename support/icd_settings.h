#ifndef ICD_SETTINGS_H
#define ICD_SETTINGS_H

#include <glib.h>

/** settings handle; for internal use only */
typedef struct {
  struct {
    gchar *network_type;
    guint network_attrs;
    gchar *network_id;
  } nw;
  /** settings backend that provides the information */
  gpointer backend;
  /** settings set for the backend, i.e. "network_types", "IAP", etc. */
  gchar *set;
} icd_settings_handle_t;

typedef icd_settings_handle_t *   icd_settings_handle;


icd_settings_handle icd_settings_get_by_network (const gchar *network_type,
                                                 const guint network_attrs,
                                                 const gchar *network_id);

gboolean icd_settings_init        (void);

gboolean icd_settings_delete      (icd_settings_handle handle);

gboolean icd_settings_is_saved    (icd_settings_handle handle);

gboolean icd_settings_save        (icd_settings_handle handle);

gboolean icd_settings_get_network (icd_settings_handle handle,
                                  gchar **network_type,
                                  guint *network_attrs,
                                  gchar **network_id);

gboolean icd_settings_get_boolean (icd_settings_handle handle,
                                  const gchar *attribute,
                                  gboolean *value);

gboolean icd_settings_set_boolean (icd_settings_handle handle,
                                  const gchar *attribute,
                                  gboolean value);

gboolean icd_settings_get_int     (icd_settings_handle handle,
                                  const gchar *attribute,
                                  gint *value);

gboolean icd_settings_set_int     (icd_settings_handle handle,
                                  const gchar *attribute,
                                  gint value);

gboolean icd_settings_get_uint    (icd_settings_handle handle,
                                  const gchar *attribute,
                                  guint *value);

gboolean icd_settings_set_uint    (icd_settings_handle handle,
                                  const gchar *attribute,
                                  guint value);

gboolean icd_settings_get_char    (icd_settings_handle handle,
                                  const gchar *attribute,
                                  gchar **value);

gboolean icd_settings_set_char    (icd_settings_handle handle,
                                  const gchar *attribute,
                                  gchar *value);

#endif
