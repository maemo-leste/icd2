#ifndef POLICY_API_H
#define POLICY_API_H

/**
@file policy_api.h

@author patrik.flykt@nokia.com

@copyright (C) 2007 Nokia Corporation. All rights reserved.

@addtogroup policy_api ICd policy API

 * @{ */

#include <glib.h>

#include "network_api.h"

/** Status of the policy check */
enum icd_policy_status {
  /** Accepted, next module to verify policy */
  ICD_POLICY_ACCEPTED = 0,
  /** Request is merged with an existing one, stops policy processing */
  ICD_POLICY_MERGED,
  /** Request is waiting for external action, stops policy processing */
  ICD_POLICY_WAITING,
  /** Rejected by policy module */
  ICD_POLICY_REJECTED
};

/** set if request is a response from UI */
#define ICD_POLICY_ATTRIBUTE_CONN_UI            0x01

/** set if an application generated the request by itself */
#define ICD_POLICY_ATTRIBUTE_BACKGROUND         0x02

/** set if a previous attempt for a request to connect networks failed; used
 * by policy_iap_ask_request() */
#define ICD_POLICY_ATTRIBUTE_CONNECTIONS_FAILED 0x04

/** set whenever any IAPs are added to the request */
#define ICD_POLICY_ATTRIBUTE_HAS_CONNECTIONS    0x08

/** set if no user interaction may take place */
#define ICD_POLICY_ATTRIBUTE_NO_INTERACTION     0x10

/** set if always online policy has made the request and the request is
 * allowed to change IAP */
#define ICD_POLICY_ATTRIBUTE_ALWAYS_ONLINE_CHANGE 0x20

/** set if always online policy has made the request */
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


/** Network scan status */
enum icd_policy_scan_status {
  /** A new network was found */
  ICD_POLICY_SCAN_NEW_NETWORK = 0,
  /** The signal strenght of the network was updated */
  ICD_POLICY_SCAN_UPDATE_NETWORK,
  /** The network is no longer found */
  ICD_POLICY_SCAN_EXPIRED_NETWORK,
  /** Scanning done for this scan request */
  ICD_POLICY_SCAN_DONE
};

/** Result of the policy decision
 * @param status        status of the operation, i.e. whether to accept or
 *                      deny the request
 * @param new_request   the new connection request or NULL if the request has
 *                      been merged with an existing one
 * @param policy_token  the received policy_token
 */
typedef void
(*icd_policy_request_new_cb_fn) (enum icd_policy_status status,
                                 struct icd_policy_request *new_request,
                                 gpointer policy_token);

/** New connection request policy function. Note that this function call is
 * asynchronous; the callback needs to be called in order to report status.
 *
 * @param new_request        the new connection request
 * @param existing_requests  currently existing requests; valid only when
 *                           this function is executing - DO NOT EVER USE
 *                           later on in any callbacks
 * @param cb                 callback to call when policy has been decided
 * @param private            the private member of the icd_request_api
 *                           structure
 * @note  Policy modules implementing this function should also check whether
 *        they need to implement #icd_policy_request_cancel_fn.
 */
typedef void
(*icd_policy_request_new_fn) (struct icd_policy_request *new_request,
                              const GSList *existing_requests,
                              icd_policy_request_new_cb_fn policy_done_cb,
                              gpointer policy_token,
                              gpointer *private);
/** Clean up internal policy module data structures for a request that has
 * previously reported #ICD_POLICY_WAITING. After returning from this
 * function, the request will be freed.
 *
 * @param request  the request that is to be removed
 * @param private  private data for the module
 */
typedef void
(*icd_policy_request_cancel_fn) (struct icd_policy_request *request,
                                 gpointer *private);


/** Network connection policy function that will be once for each network
 * that is tried.
 *
 * @param network               the network to connect
 * @param existing_connections  existing network connections
 * @param private               private data for the module
 */
typedef enum icd_policy_status
(*icd_policy_nw_connect_fn) (struct icd_policy_request *network,
                             GSList *existing_connections,
                             gpointer *private);

/** Network connection restart policy function; decides how many times the
 * network can be restarted
 *
 * @param network        the network to connect
 * @param restart_count  how many times the network module has requested
 *                       #ICD_NW_RESTART
 * @param private        private data for the module
 */
typedef enum icd_policy_status
(*icd_policy_nw_connection_restart_fn) (struct icd_policy_request *network,
                                        guint restart_count,
                                        gpointer *private);


/** Informational policy called when a network has been successfully
 * connected
 *
 * @param network               the network to connect
 * @param existing_connections  existing network connections
 * @param private               private data for the module
 */
typedef void
(*icd_policy_nw_connected_fn) (struct icd_policy_request *network,
                               GSList *existing_connections,
                               gpointer *private);

/** Network disconnection policy function called when ICd attempts to
 * disconnect a network.
 *
 * @param network               the network to disconnect
 * @param reference_count       the number of applications using this
 *                              connection or -1 on forced disconnect from
 *                              the Connectivity UI
 * @param existing_connections  existing network connections
 * @param private               private data for the module
 */
typedef enum icd_policy_status
(*icd_policy_nw_disconnect_fn) (struct icd_policy_request *network,
                                gint reference_count,
                                GSList *existing_connections,
                                gpointer *private);

/** Informational policy called when a network has been disconnected
 * @param request               the network to connect
 * @param err_str               NULL if the network was disconnected
 *                              normally, any ICD_DBUS_ERROR_* from
 *                              osso-ic-dbus.h on error
 * @param existing_connections  existing network connections
 * @param private               private data for the module
 */
typedef void
(*icd_policy_nw_disconnected_fn) (struct icd_policy_request *network,
                                  const gchar *err_str,
                                  GSList *existing_connections,
                                  gpointer *private);

/** Informationa policy called when a scan is stopped for a network type
 * @param network_type  network type
 * @param private       private data for the module
 */
typedef void (*icd_policy_nw_scan_stop_fn) (const gchar *network_type,
                                            gpointer *private);

/** Informationa policy called when a scan is started for a network type
 * @param network_type  network type
 * @param private       private data for the module
 */
typedef void (*icd_policy_nw_scan_start_fn) (const gchar *network_type,
                                             gpointer *private);

/** Policy module destruction function. Will be called before unloading the
 * module.
 *
 * @param private  a reference to the private data
 */
typedef void (*icd_policy_destruct_fn) (gpointer *private);

/** Policy module network priority function.
 * @param  srv_type       service type or NULL if none
 * @param  srv_id         service id or NULL if none
 * @param  network_type   network type
 * @param  network_attrs  network attrs
 * @param  the            network priority (returned to caller)
 * @return is there network type that has higher priority (TRUE = yes there
 *         is, FALSE = no there is not)
 */
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

/** The policy module API to be filled in by the module */
struct icd_policy_api {
  /** private data for the module */
  gpointer private;
  /** request for a new connection */
  icd_policy_request_new_fn new_request;
  /** cancelling a waiting policy request */
  icd_policy_request_cancel_fn cancel_request;

  /** whether to set up a particular network connection */
  icd_policy_nw_connect_fn connect;
  /** connection restart policy */
  icd_policy_nw_connection_restart_fn restart;
  /** informational policy when a network has been connected */
  icd_policy_nw_connected_fn connected;
  /** whether to take down a particular network connection */
  icd_policy_nw_disconnect_fn disconnect;
  /** informational policy when a network has been disconnected */
  icd_policy_nw_disconnected_fn disconnected;

  /** informational policy for scan start */
  icd_policy_nw_scan_start_fn scan_start;
  /** informational policy for scan stop */
  icd_policy_nw_scan_stop_fn scan_stop;

  /** module destruction function */
  icd_policy_destruct_fn destruct;

  /** network priority function */
  icd_policy_network_priority_fn priority;
};

/** Add a network connection to try in response to the policy decision. Any
 * policy module using this function must be compatible with the
 * corresponding network module. In order to avoid excessive string
 * allocation, copying and deletion, ICd - not the module - will free all
 * strings with g_free().
 *
 * @param request           the request this network refers to
 * @param service_type      service provider type, see #srv_provider_api.h
 * @param service_attrs     service provider attributes, see
 *                          #srv_provider_api.h
 * @param service_id        service_provider id, see #srv_provider_api.h
 * @param network_type      network type, see #network_api.h
 * @param network_attrs     network attributes, see #network_api.h
 * @param network_id        network id, see #network_api.h
 * @param network_priority  network priority, default value to use is -1
 */
typedef void
(*icd_policy_nw_add_fn) (struct icd_policy_request *request,
                         gchar *service_type,
                         guint service_attrs,
                         gchar *service_id,
                         gchar *network_type,
                         guint network_attrs,
                         gchar *network_id,
                         gint network_priority);

/** Merge two request together and free the request_to_merge structure.
 * @param request_to_merge  the request that will be joined with the existing
 *                          one. DO NOT USE after calling this function as it
 *                          is freed and can point to anything afterwards.
 * @param existing_request  the request that continues to exists after a
 *                          merge
 */
typedef void
(*icd_policy_request_merge_fn) (struct icd_policy_request *request_to_merge,
                                struct icd_policy_request *existing_request);

/** Make a request for a new network connection. The new request will
 * translate into #icd_policy_request_new_fn function calls.
 *
 * @param policy_attrs   ICD_POLICY_ATTRIBUTE_* attributes
 * @param service_type   service provider type, see #srv_provider_api.h
 * @param service_attrs  service provider attributes, see #srv_provider_api.h
 * @param service_id     service_provider id, see #srv_provider_api.h
 * @param network_type   network type, see #network_api.h
 * @param network_attrs  network attributes, see #network_api.h
 * @param network_id     network id, see #network_api.h
 */
typedef void (*icd_policy_request_make_new_fn) (guint policy_attrs,
                                                gchar *service_type,
                                                guint service_attrs,
                                                gchar *service_id,
                                                gchar *network_type,
                                                guint network_attrs,
                                                gchar *network_id);

/** Callback function for network scan.
 * @param status            network scan status with ICD_POLICY_SCAN_* values
 * @param service_name      service provider name, see #srv_provider_api.h
 * @param service_type      service provider type, see #srv_provider_api.h
 * @param service_attrs     service provider attributes, see
 *                          #srv_provider_api.h
 * @param service_id        service_provider id, see #srv_provider_api.h
 * @param service_priority  service priority within a service_type
 * @param network_name      network name, see #network_api.h
 * @param network_type      network type, see #network_api.h
 * @param network_attrs     network attributes, see #network_api.h
 * @param network_id        network id, see #network_api.h
 * @param network_priority  network priority between different network_type
 * @param signal            signal leve, see #network_api.h
 * @param user_data         user data passed to #icd_policy_scan_start_fn
 */
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

/** Request network scan. Only usable in #icd_policy_request_new_fn, as it's
 * the only asynchronous policy function that a policy module can implement.
 * Note also that scanning will continue until #icd_policy_scan_stop_fn is
 * called.
 *
 * @param type       network type
 * @param scope      scan scope
 * @param cb         callback function to call with scan results, cannot be
 *                   NULL
 * @param user_data  user data to pass to the callback function
 */
typedef void (*icd_policy_scan_start_fn) (const gchar *type,
                                          const guint scope,
                                          icd_policy_scan_cb_fn cb,
                                          gpointer user_data);

/** Stop all network scans
 * @param cb         callback function passed to #icd_policy_scan_start_fn
 * @param user_data  user data passed to #icd_policy_scan_start_fn
 */
typedef void (*icd_policy_scan_stop_fn) (icd_policy_scan_cb_fn cb,
                                         gpointer user_data);

/** Close a connected network in icd_policy_nw_(dis)?connect* functions
 * @param network  the network to disconnect
 */
typedef void (*icd_policy_nw_close_fn) (struct icd_policy_request *network);

/** Policy module initialization function. ICd will look for this type of
 * function called 'icd_policy_init' for each policy module.
 *
 * @param policy_api      policy API structure to be filled in by the module
 * @param add_network     function to add a network in response to a policy
 * @param merge_requests  function to merge requests
 * @param make_request    function for creating a new request
 * @param scan_networks   function for scanning networks
 * @param nw_close        function to disconnect a network
 * @param priority        function to get a network priority for a network
 *                        type
 * @param srv_check       function to check if there is a service module for
 *                        a given service type
 */
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

/** @} */

#endif
