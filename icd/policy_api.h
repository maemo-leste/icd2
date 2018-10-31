#ifndef POLICY_API_H
#define POLICY_API_H

#include <glib.h>

#include "network_api.h"

enum icd_policy_status {
  ICD_POLICY_ACCEPTED = 0,
  ICD_POLICY_MERGED,
  ICD_POLICY_WAITING,
  ICD_POLICY_REJECTED
};

#define ICD_POLICY_ATTRIBUTE_CONN_UI            0x01

#define ICD_POLICY_ATTRIBUTE_BACKGROUND         0x02

#define ICD_POLICY_ATTRIBUTE_CONNECTIONS_FAILED 0x04

#define ICD_POLICY_ATTRIBUTE_HAS_CONNECTIONS    0x08

#define ICD_POLICY_ATTRIBUTE_NO_INTERACTION     0x10

#define ICD_POLICY_ATTRIBUTE_ALWAYS_ONLINE_CHANGE 0x20

#define ICD_POLICY_ATTRIBUTE_ALWAYS_ONLINE      0x40

/** The requested (pseudo)network that is to be decided on by the policy
 * function. */
struct icd_policy_request {
  /** internal information */
  gpointer request_token;

  /** attributes for this request */
  guint attrs;

  /** service provider type */
  gchar *service_type;

  /** service provider attributes */
  guint service_attrs;

  /** service provider id */
  gchar *service_id;

  /** (pseudo)network type */
  gchar *network_type;

  /** (pseudo)network attributes */
  guint network_attrs;

  /** (pseudo)network id */
  gchar *network_id;

  /** network priority */
  gint network_priority;
};


enum icd_policy_scan_status {
  ICD_POLICY_SCAN_NEW_NETWORK = 0,
  ICD_POLICY_SCAN_UPDATE_NETWORK,
  ICD_POLICY_SCAN_EXPIRED_NETWORK,
  ICD_POLICY_SCAN_DONE
};

typedef void
(*icd_policy_request_new_cb_fn) (enum icd_policy_status status,
                                 struct icd_policy_request *new_request,
                                 gpointer policy_token);

typedef void
(*icd_policy_request_new_fn) (struct icd_policy_request *new_request,
                              const GSList *existing_requests,
                              icd_policy_request_new_cb_fn policy_done_cb,
                              gpointer policy_token,
                              gpointer *private);
typedef void
(*icd_policy_request_cancel_fn) (struct icd_policy_request *request,
                                 gpointer *private);


typedef enum icd_policy_status
(*icd_policy_nw_connect_fn) (struct icd_policy_request *network,
                             GSList *existing_connections,
                             gpointer *private);

typedef enum icd_policy_status
(*icd_policy_nw_connection_restart_fn) (struct icd_policy_request *network,
                                        guint restart_count,
                                        gpointer *private);


typedef void
(*icd_policy_nw_connected_fn) (struct icd_policy_request *network,
                               GSList *existing_connections,
                               gpointer *private);

typedef enum icd_policy_status
(*icd_policy_nw_disconnect_fn) (struct icd_policy_request *network,
                                gint reference_count,
                                GSList *existing_connections,
                                gpointer *private);

typedef void
(*icd_policy_nw_disconnected_fn) (struct icd_policy_request *network,
                                  const gchar *err_str,
                                  GSList *existing_connections,
                                  gpointer *private);

typedef void (*icd_policy_nw_scan_stop_fn) (const gchar *network_type,
                                            gpointer *private);

typedef void (*icd_policy_nw_scan_start_fn) (const gchar *network_type,
                                             gpointer *private);

typedef void (*icd_policy_destruct_fn) (gpointer *private);

typedef gboolean
(*icd_policy_network_priority_fn)(const gchar *srv_type,
                                  const gchar *srv_id,
                                  const gchar *network_type,
                                  const guint network_attrs,
                                  gint *network_priority);

/** Policy module service module check function.
 * @param  network_type  network type
 * @return TRUE if there is a suitable service module loaded, FALSE if not
 */
typedef gboolean
(*icd_policy_service_module_check_fn)(const gchar *network_type);

struct icd_policy_api {
  gpointer private;
  icd_policy_request_new_fn new_request;
  icd_policy_request_cancel_fn cancel_request;

  icd_policy_nw_connect_fn connect;
  icd_policy_nw_connection_restart_fn restart;
  icd_policy_nw_connected_fn connected;
  icd_policy_nw_disconnect_fn disconnect;
  icd_policy_nw_disconnected_fn disconnected;

  icd_policy_nw_scan_start_fn scan_start;
  icd_policy_nw_scan_stop_fn scan_stop;

  icd_policy_destruct_fn destruct;

  icd_policy_network_priority_fn priority;
};

typedef void
(*icd_policy_nw_add_fn) (struct icd_policy_request *request,
                         gchar *service_type,
                         guint service_attrs,
                         gchar *service_id,
                         gchar *network_type,
                         guint network_attrs,
                         gchar *network_id,
                         gint network_priority);

typedef void
(*icd_policy_request_merge_fn) (struct icd_policy_request *request_to_merge,
                                struct icd_policy_request *existing_request);

typedef void (*icd_policy_request_make_new_fn) (guint policy_attrs,
                                                gchar *service_type,
                                                guint service_attrs,
                                                gchar *service_id,
                                                gchar *network_type,
                                                guint network_attrs,
                                                gchar *network_id);

typedef void
(*icd_policy_scan_cb_fn) (const guint status,
                          const gchar *service_name,
                          const gchar *service_type,
                          const guint service_attrs,
                          const gchar *service_id,
                          gint service_priority,
                          const gchar *network_name,
                          const gchar *network_type,
                          const guint network_attrs,
                          const gchar *network_id,
                          gint network_priority,
                          const enum icd_nw_levels signal,
                          gpointer user_data);

typedef void (*icd_policy_scan_start_fn) (const gchar *type,
                                          const guint scope,
                                          icd_policy_scan_cb_fn cb,
                                          gpointer user_data);

typedef void (*icd_policy_scan_stop_fn) (icd_policy_scan_cb_fn cb,
                                         gpointer user_data);

typedef void (*icd_policy_nw_close_fn) (struct icd_policy_request *network);

typedef void
(*icd_policy_init_fn) (struct icd_policy_api *policy_api,
                       icd_policy_nw_add_fn add_network,
                       icd_policy_request_merge_fn merge_requests,
                       icd_policy_request_make_new_fn make_request,
                       icd_policy_scan_start_fn scan_start,
                       icd_policy_scan_stop_fn scan_stop,
                       icd_policy_nw_close_fn nw_close,
                       icd_policy_network_priority_fn priority,
                       icd_policy_service_module_check_fn srv_check);

#endif
