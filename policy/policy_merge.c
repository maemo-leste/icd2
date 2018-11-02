#include <osso-ic.h>

#include <string.h>

#include "policy_api.h"
#include "icd_log.h"

static gboolean
string_equal(const char *a, const char *b)
{
  if (!a)
    return !b;

  if (b)
    return !strcmp(a, b);

  return FALSE;
}

static void
icd_policy_merge_new_request(struct icd_policy_request *new_request,
                             const GSList *existing_requests,
                             icd_policy_request_new_cb_fn policy_done_cb,
                             gpointer policy_token, gpointer *private)
{
  icd_policy_request_merge_fn merge_requests =*private;
  const GSList *l;
  struct icd_policy_request *request;

  if (new_request->attrs & ICD_POLICY_ATTRIBUTE_ALWAYS_ONLINE_CHANGE &&
      string_equal(new_request->network_id, OSSO_IAP_ANY))
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
        string_equal(new_request->network_type, request->network_type))
    {
      if (string_equal(new_request->network_id, request->network_id))
        break;
    }

    if (string_equal(new_request->network_id, OSSO_IAP_ANY) &&
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
  policy_api->new_request = icd_policy_merge_new_request;
}
