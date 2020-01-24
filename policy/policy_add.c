/**
@file policy_add.c
@copyright GNU GPLv2 or later

@addtogroup policy_add New IAP creation
@ingroup policy

 * @{ */

#include "policy_api.h"

/**
 * Create a new IAP - this is the default action after all other meta IAPs
 * have been filtered away
 *
 * @param new_request        the new connection request
 * @param existing_requests  currently existing requests
 * @param policy_done_cb     callback to call when policy has been decided
 * @param policy_token       the policy token to return in the callback
 * @param private            the private member of the icd_request_api
 *                           structure
 */
static void
policy_add_request(struct icd_policy_request *new_request,
                   const GSList *existing_requests,
                   icd_policy_request_new_cb_fn policy_done_cb,
                   gpointer policy_token, gpointer *private)
{
  icd_policy_nw_add_fn add_network = *private;

  if (!(new_request->attrs & ICD_POLICY_ATTRIBUTE_HAS_CONNECTIONS))
  {
    add_network(new_request, new_request->service_type,
                new_request->service_attrs,
                new_request->service_id,
                new_request->network_type,
                new_request->network_attrs,
                new_request->network_id,
                new_request->network_priority);
  }

  policy_done_cb(ICD_POLICY_ACCEPTED, new_request, policy_token);
}

void
icd_policy_init(struct icd_policy_api *policy_api,
                icd_policy_nw_add_fn add_network,
                icd_policy_request_merge_fn merge_requests,
                icd_policy_request_make_new_fn make_request,
                icd_policy_scan_start_fn scan_start,
                icd_policy_scan_stop_fn scan_stop,
                icd_policy_nw_close_fn nw_close,
                icd_policy_network_priority_fn priority,
                icd_policy_service_module_check_fn srv_check)
{
  policy_api->private = add_network;
  policy_api->new_request = policy_add_request;
}

/** @} */
