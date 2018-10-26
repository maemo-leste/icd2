#ifndef ICD_REQUEST_H
#define ICD_REQUEST_H

#include <dbus/dbus.h>
#include <glib.h>
#include "policy_api.h"
#include "icd_tracking_info.h"
#include "icd_iap.h"

/** status of the request */
enum icd_request_status {
  /** the request is pending in the policy framework */
  ICD_REQUEST_POLICY_PENDING = 0,

  /** the request is waiting for an external event such as scan results, UI
   *  dialog, etc; stops policy processing */
  ICD_REQUEST_WAITING,

  /** change to this request when the other request has been closed */
  ICD_REQUEST_CHANGETO,

  /** request was merged with an existing one */
  ICD_REQUEST_MERGED,

  /** establishing connection(s) */
  ICD_REQUEST_CONNECTING_IAPS,

  /** the request was successfully completed */
  ICD_REQUEST_SUCCEEDED,

  /** the request was denied */
  ICD_REQUEST_DENIED,

  /** the IAP and network connection mapping to the request was successfully
   *  closed down */
  ICD_REQUEST_DISCONNECTED,


  /* max number of request statuses */
  /** max number of request statuses */
  ICD_REQUEST_MAX
};

/**
 * @brief  The request status callback function
 *
 * @param  status     the outcome of the request
 * @param  user_data  user data
 */
typedef void (*icd_request_cb_fn) (enum icd_request_status status,
                                   gpointer user_data);

/** ICd connection request. An #icd_request exists while the IAP is being
 *  created and is deleted when the IAP has successfully connected. */
struct icd_request {

  /** current request state */
  enum icd_request_status state;

  /** list of requesting D-Bus clients */
  GSList *users;

  /** whether more than one iap is to be tried, used if none of the IAPs are
   *  successfully connected */
  gboolean multi_iaps;

  /** List of IAPs to try */
  GSList *try_iaps;

  /** what this request is all about */
  struct icd_policy_request req;
};

/**
 * @brief  Function called for each request structure
 *
 * @param  request    the request
 * @param  user_data  user data passed to #icd_request_foreach()
 *
 * @return NULL to continue iteration, non-NULL to stop the iteration and
 *         return this pointer in #icd_request_foreach().
 */
typedef gpointer (*icd_request_foreach_fn) (struct icd_request *request,
                                            gpointer user_data);

gpointer icd_request_foreach (icd_request_foreach_fn fn,
                              gpointer user_data);
void icd_request_free_iaps (struct icd_request *request);
struct icd_request *icd_request_find (const gchar *network_type,
                                      const guint network_attrs,
                                      const gchar *network_id);
struct icd_request *icd_request_find_by_iap (const gchar *network_type,
                                             const guint network_attrs,
                                             const gchar *network_id);
struct icd_request *icd_request_find_by_iap_id (const gchar *iap_id,
                                                const gboolean is_local);
void icd_request_send_ack (struct icd_request *request,
                           struct icd_iap *iap);
void icd_request_send_nack (struct icd_request *request);
void icd_request_add_iap (struct icd_request *request,
                          gchar *service_type,
                          guint service_attrs,
                          gchar *service_id,
                          gchar *network_type,
                          guint network_attrs,
                          gchar *network_id,
                          gint network_priority);
struct icd_request *icd_request_new (guint policy_attrs,
                                     const gchar *service_type,
                                     const guint service_attrs,
                                     const gchar *service_id,
                                     const gchar *network_type,
                                     const guint network_attrs,
                                     const gchar *network_id);
void icd_request_make (struct icd_request *request);
gboolean icd_request_merge (struct icd_request *merge_request,
                            struct icd_request *existing);
void icd_request_cancel (struct icd_request *request, guint policy_attrs);
void icd_request_tracking_info_remove (struct icd_request *request,
                                       struct icd_tracking_info *track);
gboolean icd_request_tracking_info_delete (const gchar *sender);
void icd_request_tracking_info_add (struct icd_request *request,
                                    struct icd_tracking_info *track);


#endif
