/**
@file icd_gconf.c

@authors timo.teras@nokia.com
         patrik.flykt@nokia.com
         aapo.makela@nokia.com
         jukka.rissanen@nokia.com

@copyright (C) 2004-2008 Nokia Corporation. All rights reserved.

@addtogroup icd_gconf Compatibility functions for gconf settings

@ingroup internal

 * @{ */

#include <string.h>
#include <gconf/gconf-client.h>
#include <osso-ic-gconf.h>
#include "icd_log.h"
#include "icd_gconf.h"
#include "icd_context.h"
#include "icd_scan.h"

/**
 * Print out a gconf error
 * @param error  reference to the GError
 */
static void
icd_gconf_check_error(GError **error)
{
  if (error && *error)
  {
    ILOG_ERR("icd gconf error: %s", (*error)->message);
    g_clear_error(error);
    *error = NULL;
  }
}

/**
 * Get a boolean value from gconf
 *
 * @param  iap_name  unescaped gconf IAP name
 * @param  key_name  key name
 * @param  def       the default expected value
 *
 * @return the value of the key
 */
gboolean
icd_gconf_get_iap_bool(const char *iap_name, const char *key_name, gboolean def)
{
  GConfClient *gconf = gconf_client_get_default();
  gchar *key;
  GConfValue *val;
  gboolean rv = def;
  GError *err = NULL;

  if (iap_name)
  {
    gchar *s = gconf_escape_key(iap_name, -1);
    key = g_strdup_printf(ICD_GCONF_PATH  "/%s/%s", s, key_name);
    g_free(s);
  }
  else
    key = g_strdup_printf(ICD_GCONF_PATH "/%s", key_name);

  val = gconf_client_get(gconf, key, &err);
  g_free(key);
  icd_gconf_check_error(&err);

  if (val)
  {
    if (val->type == GCONF_VALUE_BOOL)
      rv = gconf_value_get_bool(val);

    gconf_value_free(val);
  }

  g_object_unref(gconf);

  return rv;
}

/**
 * Check whether the setting is temporary
 * @param  settings_name  name of the IAP
 * @return TRUE if yes, FALSE if not
 */
gboolean
icd_gconf_is_temporary(const gchar *settings_name)
{
  if (!settings_name)
    return FALSE;

  if (!icd_gconf_get_iap_bool(settings_name, ICD_GCONF_IAP_IS_TEMPORARY, FALSE))
  {
    if (!strncmp(settings_name, "[Easy", 5))
      ILOG_DEBUG("settings is temp IAP because of '[Easy' name prefix");

    return FALSE;
  }
  else
    ILOG_DEBUG("setting is temp IAP because of 'temporary' key");

  return TRUE;
}

/**
 * Remove a gconf directory if it is a temporary IAP
 * @param  settings_name  escaped IAP name
 * @return TRUE if the gconf directory got removed, FALSE otherwise
 */
static gboolean
icd_gconf_remove_dir(const gchar *settings_name)
{
  GConfClient *gconf = gconf_client_get_default();
  gboolean rv = FALSE;
  GError *err = NULL;

  if (icd_gconf_is_temporary(settings_name))
  {
    gchar *s = gconf_escape_key(settings_name, -1);
    gchar *key = g_strdup_printf(ICD_GCONF_PATH "/%s", s);

    g_free(s);
    gconf_client_recursive_unset(gconf, key, GCONF_UNSET_INCLUDING_SCHEMA_NAMES,
                                 &err);

    if (err)
      icd_gconf_check_error(&err);
    else
    {
      ILOG_DEBUG("icd gconf removed '%s' from gconf", key);
      rv = TRUE;
    }

    g_free(key);
  }

  g_object_unref(gconf);

  return rv;
}

/**
 * Remove temporary IAPs from gconf
 * @param  settings_name  name of temporary IAP to remove or NULL for all
 * @return TRUE if at least one IAP was removed, FALSE otherwise
 */
gboolean
icd_gconf_remove_temporary(const gchar *settings_name)
{
  GConfClient *gconf;
  GError *err = NULL;
  gboolean rv = FALSE;
  GSList *l;

  if (settings_name)
    return icd_gconf_remove_dir(settings_name);

  gconf = gconf_client_get_default();

  l = gconf_client_all_dirs(gconf, ICD_GCONF_PATH, &err);

  if (err)
  {
    g_object_unref(gconf);
    icd_gconf_check_error(&err);
    return FALSE;
  }

  while (l)
  {
    const gchar *p = g_strrstr((const gchar *)l->data, "/");

    if (p)
    {
      gchar *dir = gconf_unescape_key(p + 1, -1);

      if (icd_gconf_remove_dir(dir))
        rv = TRUE;

      g_free(dir);
    }

    g_free(l->data);
    l = g_slist_remove_link(l, l);
  }

  g_object_unref(gconf);

  return rv;
}

/** Notice if IAP is removed in gconf. */
static void
icd_gconf_notify(GConfClient *gconf_client, guint connection_id,
                 GConfEntry *entry, gpointer user_data)
{
  size_t len = strlen(ICD_GCONF_PATH "/");
  const char *key = gconf_entry_get_key(entry);

  if (!strncmp(key, ICD_GCONF_PATH "/", len) && !strchr(&key[len], '/') &&
      !gconf_entry_get_value(entry))
  {
    gchar *iap_name = gconf_unescape_key(&key[len], -1);

    ILOG_DEBUG("IAP (%s) deletion detected, checking cache", iap_name);

    icd_scan_cache_remove_iap(iap_name);
    g_free(iap_name);
  }
}

/**
 * Set notification func for gconf changes
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_gconf_add_notify(void)
{
  struct icd_context *icd_ctx = icd_context_get();
  GConfClient *gconf = gconf_client_get_default();

  icd_ctx->iap_deletion_notify =
      gconf_client_notify_add(gconf, ICD_GCONF_PATH, icd_gconf_notify, NULL,
                              NULL, NULL);
  g_object_unref(gconf);

  return icd_ctx->iap_deletion_notify != 0;
}

/**
 * Rename settings
 *
 * @param  settings_name  current IAP settings name
 * @param  name           new name of the settings
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_gconf_rename(const gchar *settings_name, const gchar *name)
{
  if (settings_name && *settings_name && name && *name)
  {
    GConfClient *gconf = gconf_client_get_default();
    char *id = gconf_escape_key(settings_name, -1);
    gchar *key = g_strdup_printf(ICD_GCONF_PATH "/%s/name", id);

    if (!gconf_client_set_string(gconf, key, name, NULL))
      ILOG_ERR("settings could not save '%s' to '%s'", name, key);

    g_free(key);

    key = g_strdup_printf(ICD_GCONF_PATH "/%s/temporary", id);

    if (!gconf_client_unset(gconf, key, NULL))
      ILOG_ERR("settings could not unset '%s'", key);

    g_free(key);
    g_free(id);
    g_object_unref(gconf);
    return TRUE;
  }

  ILOG_ERR("settings '%s' and name '%s' must both be non-empty",
           settings_name, name);

  return FALSE;
}

/**
 * Get a string from gconf
 *
 * @param  iap_name  unescaped gconf IAP name
 * @param  key_name  key name
 *
 * @return the key value which is to be freed by the caller or NULL if the
 *         value does not exist
 */
gchar *
icd_gconf_get_iap_string(const char *iap_name, const char *key_name)
{
  GConfClient *gconf = gconf = gconf_client_get_default();
  GError *err = NULL;
  char *id = gconf_escape_key(iap_name, -1);
  gchar *key = g_strdup_printf(ICD_GCONF_PATH "/%s/%s", id, key_name);
  gchar *rv;

  g_free(id);
  rv = gconf_client_get_string(gconf, key, &err);
  g_free(key);
  icd_gconf_check_error(&err);
  g_object_unref(gconf);

  return rv;
}

/** Remove notification func for gconf changes */
void
icd_gconf_del_notify(void)
{
  struct icd_context *icd_ctx = icd_context_get();
  GConfClient *gconf = gconf_client_get_default();

  gconf_client_notify_remove(gconf, icd_ctx->iap_deletion_notify);
  icd_ctx->iap_deletion_notify = 0;
  g_object_unref(gconf);
}

/** @} */
