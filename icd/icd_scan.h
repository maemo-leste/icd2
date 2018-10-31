#ifndef ICD_SCAN_H
#define ICD_SCAN_H

/**
@file icd_scan.h

@author patrik.flykt@nokia.com

@copyright (C) 2007 Nokia Corporation. All rights reserved.

@addtogroup icd_scan Network scan and scan result handling

Internally each module has a hash table of cached scan results. The hash
table is accessed using the module internal network_id. Each hash table entry
contains an #icd_scan_cache_list structure with a singly linked list of
networks. The #icd_scan_cache_list structure is used because the pointer to
the singly linked list must be updated whenever a network is removed.
<pre>
 +---+
 | n |   +GHashTable(network_idX)-+
 | w |--&gt;|scan_cache_table        |
 |   |   +------------------------+
 | m |                     |  |  +-&gt;#icd_scan_cache_list
 | o |                     |  |       +-&gt;GSlist for network_idN
 | d |                     |  |
 | u |                     |  +-&gt;#icd_scan_cache_list
 | l |                     |       +-&gt;GSList for network_id2
 | e |                     |
 +---+                     |   ...
                           |
                           +-&gt;#icd_scan_cache_list
                                +-&gt;GSList for network_idN
 </pre>

@ingroup internal

 * @{ */

#include <glib.h>

#include "network_api.h"
#include "dbus_api.h"

/** scan cache hash table elements defined like this because we need to
 * update the GSList pointer when elements are removed */
struct icd_scan_cache_list {
  /** list of #icd_scan_cache elements */
  GSList *cache_list;
};

/** service provider data */
struct icd_scan_srv_provider {
  /** service type */
  gchar *service_type;

  /** service level name displayable to the user */
  gchar *service_name;

  /** service attributes */
  guint service_attrs;

  /** service level id */
  gchar *service_id;

  /** service priority inside a service_type */
  gint service_priority;
};

/** cached scanned networks */
struct icd_scan_cache {
  /** time when the entry was added or updated */
  guint last_seen;

  /** type of network */
  gchar *network_type;

  /** name of the network displayable to user */
  gchar *network_name;

  /** network attributes */
  guint network_attrs;

  /** network id */
  gchar *network_id;

  /** network priority between different network_type */
  gint network_priority;

  /** signal level */
  enum icd_nw_levels signal;

  /** base station MAC address */
  gchar *station_id;

  /** raw signal strength */
  gint dB;

  /** list of service providers for this network, see #icd_scan_srv_provider */
  GSList *srv_provider_list;
};

/** #icd_network_module scan_timeout_list data. Cache expiry callbacks and
 * rescan callbacks will be queued with a pointer to this element with id
 * field containing the glib timeout id */
struct icd_scan_cache_timeout {
  /** (back)pointer to the network module */
  struct icd_network_module *module;

  /** timeout id */
  guint id;
};

/** Scan callback function for receiving scan results
 * @param status        status of this network
 * @param srv_provider  service provider entry; guaranteed to exist only for
 *                      the lifetime of this callback function
 * @param cache_entry   scan results; guaranteed to exist only for the
 *                      lifetime of this callback function
 * @param user_data     used data given to the scan callback
 */
typedef void
(*icd_scan_cb_fn) (enum icd_scan_status status,
                   const struct icd_scan_srv_provider *srv_provider,
                   const struct icd_scan_cache *cache_entry,
                   gpointer user_data);

void icd_scan_cache_entry_free (struct icd_scan_cache *cache_entry);

void icd_scan_cache_entry_add (struct icd_network_module *module,
                               struct icd_scan_cache_list *scan_cache,
                               struct icd_scan_cache *cache_entry);

struct icd_scan_cache_list *
icd_scan_cache_list_lookup (struct icd_network_module *module,
                            const gchar *network_id);

struct icd_scan_cache *
icd_scan_cache_entry_find (struct icd_scan_cache_list *scan_cache_list,
                           const gchar *network_type,
                           const guint network_attrs);

gboolean icd_scan_cache_entry_remove(struct icd_scan_cache_list *scan_cache_list,
                                     const gchar *network_id,
                                     const gchar *network_type,
                                     const guint network_attrs);

void icd_scan_listener_notify (struct icd_network_module *module,
                               struct icd_scan_srv_provider *srv_provider,
                               struct icd_scan_cache *cache_entry,
                               enum icd_scan_status status);

gboolean icd_scan_results_request (const gchar *type,
                                   const guint scope,
                                   icd_scan_cb_fn cb,
                                   gpointer user_data);
gboolean icd_scan_results_unregister (icd_scan_cb_fn cb,
                                      gpointer user_data);
gboolean icd_scan_cache_init (struct icd_network_module *module);
void icd_scan_cache_remove (struct icd_network_module *module);

void icd_scan_cache_remove_iap(gchar *iap_name);

/** @} */

#endif
