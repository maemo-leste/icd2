#include <gconf/gconf-client.h>
#include <osso-ic-gconf.h>

#include <string.h>

#include "icd_log.h"

#include "icd_backend_gconf.h"

typedef gboolean (*icd_backend_handler_fn)(gchar *, icd_settings_handle_t *);

struct icd_backend_handler_t
{
  gchar *network_type;
  icd_backend_handler_fn cb;
};

typedef struct icd_backend_handler_t icd_backend_handler;

static gboolean icd_backend_gconf_wlan_handler(gchar *network_type, icd_settings_handle handle);

static icd_backend_handler icd_backend_type_handlers[] =
{
  {"WLAN_INFRA", icd_backend_gconf_wlan_handler},
  {"WLAN_ADHOC", icd_backend_gconf_wlan_handler},
  {NULL, NULL}
};

icd_settings_handle
icd_backend_gconf_get_by_network(const gchar *network_type,
                                 const guint network_attrs,
                                 const gchar *network_id)
{
  return NULL;
}

void
icd_backend_gconf_delete(icd_settings_handle handle)
{
  gchar *key;
  GConfClient *  gconf = gconf_client_get_default();
  GError *err = NULL;

  key = g_strconcat(ICD_GCONF_PATH, "/", handle->nw.network_id, NULL);
  gconf_client_recursive_unset(
        gconf, key, GCONF_UNSET_INCLUDING_SCHEMA_NAMES, &err);

  if (err)
  {
    ILOG_DEBUG("icd backend gconf could not remove '%s': %s", key,
               err->message);
    g_clear_error(&err);
  }

  g_free(key);
  g_object_unref(gconf);
  g_free(handle->nw.network_id);
  g_free(handle);
}

void
icd_backend_gconf_init(struct icd_settings *settings)
{
  GConfClient *gconf= gconf_client_get_default();
  GSList *l;

  for (l = gconf_client_all_dirs(gconf, ICD_GCONF_PATH, NULL); l;
       l = g_slist_delete_link(l, l))
  {
    if (l->data)
    {
      icd_settings_handle handle = g_new0(icd_settings_handle_t, 1);
      gchar *network_type;
      gchar *key;

#pragma message "****************************************************"
#pragma message "\n"
#pragma message "!!!!!!!!!     Check why this is needed      !!!!!!!!"
#pragma message "\n"
#pragma message "****************************************************"
      g_strrstr((const gchar *)l->data, "/");
      handle->set = ICD_GCONF_PATH;

      key = g_strconcat((const gchar *)l->data, "/", "type", NULL);
      network_type = gconf_client_get_string(gconf, key, NULL);
      g_free(key);

      if (network_type)
      {
        icd_backend_handler *handler;

        for (handler = icd_backend_type_handlers; handler->network_type;
             handler++)
        {
          if (!strcmp(network_type, handler->network_type))
          {
            if (handler->cb && handler->cb(handler->network_type, handle))
              icd_settings_add_handle(settings, handle);
            else
              break;
          }
        }

        icd_backend_gconf_delete(handle);
      }

      g_free(network_type);
    }

    g_free(l->data);
  }
}

static gboolean
icd_backend_gconf_wlan_handler(gchar *network_type,
                               icd_settings_handle handle)
{
  GConfClient *gconf = gconf_client_get_default();
  GConfValue *val;
  gchar *key = g_strconcat(handle->set, "/", handle->nw.network_id, NULL);
  gchar *network_id;

  handle->nw.network_type = network_type;
  val = gconf_client_get(gconf, key, NULL);

  if (val)
  {
    if (val->type == GCONF_VALUE_STRING)
      network_id = g_strdup(gconf_value_get_string(val));
    else if (val->type == GCONF_VALUE_LIST &&
             gconf_value_get_list_type(val) == GCONF_VALUE_INT)
    {
      GSList *l = gconf_value_get_list(val);
      int i = 0;

      network_id = g_new(gchar, g_slist_length(l) + 1);

      while (l)
      {
        network_id[i++] = gconf_value_get_int((const GConfValue *)l->data);
        l = l->next;
      }

      network_id[i] = 0;
    }
    else
      network_id = NULL;

    gconf_value_free(val);
  }
  else
    network_id = NULL;

  handle->nw.network_id = network_id;
  g_free(key);

  return TRUE;
}
