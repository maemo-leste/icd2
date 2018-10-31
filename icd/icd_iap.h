#ifndef ICD_IAP_H
#define ICD_IAP_H

/**
@file icd_iap.h

@author patrik.flykt@nokia.com

@copyright (C) 2007 Nokia Corporation. All rights reserved.

@addtogroup icd_iap IAP connection abstraction

@ingroup internal

 * @{ */

#include "policy_api.h"

/** State of an IAP */
enum icd_iap_state {
  /** iap is not connected */
  ICD_IAP_STATE_DISCONNECTED = 0,

  /** iap pre-up script is being run */
  ICD_IAP_STATE_SCRIPT_PRE_UP,

  /** iap is connecting its link layer */
  ICD_IAP_STATE_LINK_UP,

  /** iap is connecting its post link layer, i.e. link authentication */
  ICD_IAP_STATE_LINK_POST_UP,

  /** iap is connecting its ip layer */
  ICD_IAP_STATE_IP_UP,

  /** srv module is being run */
  ICD_IAP_STATE_SRV_UP,

  /** iap (post-)up script is being run */
  ICD_IAP_STATE_SCRIPT_POST_UP,

  /** connection is being saved */
  ICD_IAP_STATE_SAVING,

  /** iap is connected */
  ICD_IAP_STATE_CONNECTED,

  /** iap is being disconnected */
  ICD_IAP_STATE_CONNECTED_DOWN,

  /** srv module is disconnecting */
  ICD_IAP_STATE_SRV_DOWN,

  /** iap is disconnecting its ip layer */
  ICD_IAP_STATE_IP_DOWN,

  /** iap is restarting at ip layer and running post-down and pre-up scripts */
  ICD_IAP_STATE_IP_RESTART_SCRIPTS,

  /** iap is disconnecting its pre link layer, i.e. link deauthenticating */
  ICD_IAP_STATE_LINK_PRE_DOWN,

  /** iap is restarting at pre link layer and running post-down and pre-up
   * scripts */
  ICD_IAP_STATE_LINK_PRE_RESTART_SCRIPTS,

  /** iap is disconnecting its link layer */
  ICD_IAP_STATE_LINK_DOWN,

  /** iap is restarting at link layer and running post-down and pre-up
   * scripts */
  ICD_IAP_STATE_LINK_RESTART_SCRIPTS,

  /** iap post-down script is being run */
  ICD_IAP_STATE_SCRIPT_POST_DOWN,

  /** number of states */
  ICD_IAP_MAX_STATES
};

/** status of the request */
enum icd_iap_status {
  /** a new IAP was created and connected succesfully */
  ICD_IAP_CREATED = 0,

  /** the iap was successfully disconnected */
  ICD_IAP_DISCONNECTED,

  /** iap failed because some other module was in use by another iap */
  ICD_IAP_BUSY,

  /** the iap failed with some critical error while connecting */
  ICD_IAP_FAILED
};

const gchar* icd_iap_state_names[ICD_IAP_MAX_STATES];

struct icd_iap;

/** The IAP status callback function
 * @param status     the status from the IAP creation process
 * @param iap        the IAP
 * @param user_data  user data
 */
typedef void (*icd_iap_request_cb_fn) (enum icd_iap_status status,
                                       struct icd_iap *iap,
                                       gpointer user_data);

/** Stored disconnect function and private data */
struct icd_iap_disconnect_data {
  /** the network module function to call */
  gpointer function;

  /** the network module private data */
  gpointer *private;
};

/** structure for storing script environment variables */
struct icd_iap_env {
  /** address family */
  gchar *addrfam;

  /** env var list */
  GSList *envlist;
};

/** Definition of a real network IAP */
struct icd_iap {
  /** unique id of this IAP */
  gchar *id;

  /** Whether the id is local to icd2 only or a globally known one found in
   * gconf.
   *
   * @note this flag is no longer needed when we have a settings library
   */
  gboolean id_is_local;


  /** what state the IAP currently is in */
  enum icd_iap_state state;

  /** limited service provider connectivity */
  gboolean limited_conn;


  /** service level name displayable to the user */
  gchar *service_name;

  /** name of the network displayable to user */
  gchar *network_name;

  /** service and network indetification attributes compatible with the
   * policy framework due to #ICD_NW_RESTART policy check */
  struct icd_policy_request connection;

  /** network interface */
  gchar *interface_name;

  /** idle timer id */
  guint idletimer_id;


  /** module that is busy serving other IAPs causing current IAP to fail */
  struct icd_network_module *busy;


  /** list of network modules associated with this network type */
  GSList *network_modules;

  /** current network module */
  GSList *current_module;

  /** list of icd_iap_disconnect ip down functions to call on disconnect */
  GSList *ip_down_list;

  /** list of icd_iap_disconnect link pre down functions to call on
   * disconnect */
  GSList *link_pre_down_list;

  /** list of link down functions to call on disconnect */
  GSList *link_down_list;


  /** service provider connect callback */
  gpointer srv_connect_cb;

  /** service provider connect user data */
  gpointer srv_connect_cb_user_data;

  /** service provider disconnect callback */
  gpointer srv_disconnect_cb;

  /** service provider disconnect user data */
  gpointer srv_disconnect_cb_user_data;


  /** what layer to restart */
  enum icd_nw_layer restart_layer;

  /** what state the restart came from; used to figure out whether network
   * scripts need to be run */
  enum icd_iap_state restart_state;

  /** monitor how many times the IAP has been restarted */
  guint restart_count;

  /** what layer to renew */
  enum icd_nw_layer renew_layer;

  /** what module is being renewed */
  GSList *current_renew_module;


  /** whether the module did all the user prompting and retry dialogs are not
   * needed */
  gboolean user_interaction_done;

  /** error that caused iap to fail */
  gchar *err_str;


  /** opaque token for save dialog request */
  gpointer save_dlg;

  /** request status callback */
  icd_iap_request_cb_fn request_cb;

  /** user data to pass to the callback */
  gpointer request_cb_user_data;

  /** list of struct icd_iap_env environment variables */
  GSList *script_env;

  /** list of script pids being waited for */
  GSList *script_pids;
};

/** Iterator function called for each active IAP structure starting from the
 * structure associated with the newest request. Only active IAPs are
 * iterated through, not the ones in a request that will be tried if the
 * current one fails.
 *
 * @param  iap        the IAP struct
 * @param  user_data  user data
 * @return TRUE to continue, FALSE to stop iterating
 */
typedef gboolean (*icd_iap_foreach_fn) (struct icd_iap *iap,
                                        gpointer user_data);


void icd_iap_free (struct icd_iap *iap);
struct icd_iap *icd_iap_new (void);
gboolean icd_iap_id_create (struct icd_iap *iap, const gchar *new_name);
void icd_iap_connect (struct icd_iap* iap,
                      icd_iap_request_cb_fn request_cb,
                      gpointer user_data);
void icd_iap_disconnect (struct icd_iap *iap, const gchar *err_str);

void icd_iap_renew (struct icd_iap *iap, enum icd_nw_layer renew_layer);

void icd_iap_restart (struct icd_iap *iap, enum icd_nw_layer restart_layer);
guint icd_iap_get_ipinfo (struct icd_iap *iap,
                          icd_nw_ip_addr_info_cb_fn cb,
                          gpointer user_data);
gboolean icd_iap_get_ip_stats (struct icd_iap *iap,
                               icd_nw_ip_stats_cb_fn cb,
                               gpointer user_data);
gboolean icd_iap_get_link_post_stats (struct icd_iap *iap,
                                      icd_nw_link_post_stats_cb_fn cb,
                                      gpointer user_data);
gboolean icd_iap_get_link_stats (struct icd_iap *iap,
                                 icd_nw_link_stats_cb_fn cb,
                                 gpointer user_data);
struct icd_iap* icd_iap_find (const gchar *network_type,
                              const guint network_attrs,
                              const gchar *network_id);
struct icd_iap* icd_iap_find_by_id (const gchar *iap_id,
                                    const gboolean is_local);
struct icd_iap *icd_iap_foreach (icd_iap_foreach_fn fn, gpointer user_data);
gboolean icd_iap_rename (struct icd_iap *iap, const gchar *name);

/** @} */

#endif
