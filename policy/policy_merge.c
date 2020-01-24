/**
@file policy_merge.c
@copyright GNU GPLv2 or later

@addtogroup policy_merge Merge a new request with an already existing one
@ingroup policy

 * @{ */

#include <osso-ic.h>

#include <string.h>

#include "policy_api.h"
#include "icd_log.h"


/**
 * Helper function for comparing two strings where a NULL string is equal to
 * another NULL string
 *
 * @param a  string A
 * @param b  string B
 *
 * @return   TRUE if equal, FALSE if unequal
 */
static gboolean
policy_merge_string_equal(const gchar *a, const gchar *b)
{
  if (!a)
    return !b;

  if (b)
    return !strcmp(a, b);

  return FALSE;
}

/**
 * Merge a new request with an already existing one
 *
 * @param new_request        the new connection request
 * @param existing_requests  currently existing requests
 * @param policy_done_cb     callback to call when policy has been decided
 * @param policy_token       token to pass to the callback
 * @param private            the private member of the icd_request_api
 *                           structure
 */
static void
policy_merge_request(struct icd_policy_request *new_request,
                     const GSList *existing_requests,
                     icd_policy_request_new_cb_fn policy_done_cb,
                     gpointer policy_token, gpointer *private)
{
  icd_policy_request_merge_fn merge_requests = *private;
  const GSList *l;
  struct icd_policy_request *request;

  if (new_request->attrs & ICD_POLICY_ATTRIBUTE_ALWAYS_ONLINE_CHANGE &&
      policy_merge_string_equal(new_request->network_id, OSSO_IAP_ANY))
  {
    ILOG_DEBUG("policy merge allows OSSO_IAP_ANY since changing while connected is allowed");
    policy_done_cb(ICD_POLICY_ACCEPTED, new_request, policy_token);
    return;
  }

  for (l = existing_requests; l; l = l->next)
  {
    request = (struct icd_policy_request *)l->data;

    ILOG_DEBUG("policy merge sees request %p, %s/%0x/%s",
               request->request_token, request->network_type,
               request->network_attrs, request->network_id);

    if ((new_request->network_attrs & ICD_NW_ATTR_LOCALMASK) ==
        (request->network_attrs & ICD_NW_ATTR_LOCALMASK) &&
        policy_merge_string_equal(
               new_request->network_type, request->network_type) )
    {
      if (policy_merge_string_equal(new_request->network_id,
                                    request->network_id) )
        break;
    }

    if (policy_merge_string_equal(new_request->network_id, OSSO_IAP_ANY) &&
        !(new_request->attrs & ICD_POLICY_ATTRIBUTE_HAS_CONNECTIONS))
    {
      break;
    }
  }

  if (l)
  {
    ILOG_INFO("Merging policy req %p with policy req %p, %s/%0x/%s",
              new_request, request, request->network_type,
              request->network_attrs, request->network_id);
    merge_requests(new_request, request);
    policy_done_cb(ICD_POLICY_MERGED, NULL, policy_token);
  }
  else
  {
    if (new_request->attrs & ICD_POLICY_ATTRIBUTE_BACKGROUND)
    {
      ILOG_INFO("policy merge got req %p with attribute ICD_POLICY_ATTRIBUTE_BACKGROUND, rejecting it",
                new_request);
      policy_done_cb(ICD_POLICY_REJECTED, new_request, policy_token);
    }
    else
      policy_done_cb(ICD_POLICY_ACCEPTED, new_request, policy_token);
  }
}

/**
 * Policy module initialization function.
 *
 * @param policy_api      policy API structure to be filled in by the module
 * @param add_network     function to add a network in response to a policy
 * @param merge_requests  function to merge requests
 */
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
  policy_api->private = merge_requests;
  policy_api->new_request = policy_merge_request;
}

/** @} */
