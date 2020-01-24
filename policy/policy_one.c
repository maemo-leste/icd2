/**
@file policy_one.c
@copyright GNU GPLv2 or later

@addtogroup policy_one One connection only
@ingroup policy

 * @{ */

#include "policy_api.h"
#include "icd_log.h"


/**
 * Informational policy called when a network has been successfully connected
 *
 * @param network               the network to connect
 * @param existing_connections  existing network connections
 * @param private               private data for the module
 */
static void
policy_one_connected(struct icd_policy_request *network,
                     GSList *existing_connections, gpointer *private)
{
  icd_policy_nw_close_fn nw_close = (icd_policy_nw_close_fn)*private;
  GSList *l;

  ILOG_INFO("policy one got connected notification");

  for (l = existing_connections; l; l = l->next)
  {
    struct icd_policy_request *request = (struct icd_policy_request *)l->data;

    if (request != network)
    {
      ILOG_INFO("policy one disconnecting connection %p", request);
      nw_close(request);
    }
    else
      ILOG_INFO("policy one found the same connection");
  }
}

/**
 * Policy module initialization function.
 *
 * @param policy_api      policy API structure to be filled in by the module
 * @param add_network     function to add a network in response to a policy
 * @param merge_requests  function to merge requests
 * @param make_request    function for creating a new request
 * @param scan_networks   function for scanning networks
 * @param nw_close        function to disconnect a network
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
  policy_api->private = nw_close;
  policy_api->connected = policy_one_connected;
}

/** @} */
