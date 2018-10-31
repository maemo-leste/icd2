/**
@file icd_request.c

@author patrik.flykt@nokia.com

@copyright (C) 2007 Nokia Corporation. All rights reserved.

@addtogroup icd_request Connection request

@ingroup internal

 * @{ */

#include <string.h>
#include <osso-ic.h>
#include <osso-ic-dbus.h>
#include "icd_request.h"
#include "icd_context.h"
#include "icd_iap.h"
#include "icd_log.h"
#include "policy_api.h"
#include "icd_policy_api.h"
#include "network_api.h"
#include "icd_osso_ic.h"
#include "icd_status.h"
#include "icd_gconf.h"
#include "icd_dbus_api.h"
#include "icd_name_owner.h"
#include "icd_network_priority.h"

static void icd_request_try_iap_cb(enum icd_iap_status status,
                                   struct icd_iap *iap, gpointer user_data);

/** ICd request status names */
static const gchar *icd_request_status_names[ICD_REQUEST_MAX] = {
  "ICD_REQUEST_POLICY_PENDING",
  "ICD_REQUEST_WAITING",
  "ICD_REQUEST_CHANGETO",
  "ICD_REQUEST_MERGED",
  "ICD_REQUEST_CONNECTING_IAPS",
  "ICD_REQUEST_SUCCEEDED",
  "ICD_REQUEST_DENIED",
  "ICD_REQUEST_DISCONNECTED"
};

/**
 * Helper function for comparing two strings where a NULL string is equal to
 * another NULL string
 *
 * @param  a  string A
 * @param  b  string B
 *
 * @return TRUE if equal, FALSE if unequal
 */
inline static gboolean
icd_request_string_equal(const char *a, const char *b)
{
  if (!a)
    return !b;

  if (b)
    return !strcmp(a, b);

  return FALSE;
}

/**
 * Foreach function for network finding
 *
 * @param  request    the request
 * @param  user_data  the network to search for
 *
 * @return the request that has a matching network connection or NULL if none
 */
static gpointer
icd_request_find_foreach(struct icd_request *request, gpointer user_data)
{
  struct icd_policy_request *preq = (struct icd_policy_request *)user_data;
  guint pattrs = preq->network_attrs;
  guint attrs = request->req.network_attrs;

  if (((pattrs & ICD_NW_ATTR_IAPNAME) == (attrs & ICD_NW_ATTR_IAPNAME)) &&
      ((attrs & ICD_NW_ATTR_LOCALMASK) == (pattrs & ICD_NW_ATTR_LOCALMASK)) &&
      icd_request_string_equal(request->req.network_type, preq->network_type) &&
      icd_request_string_equal(request->req.network_id, preq->network_id) )
  {

    ILOG_DEBUG("found request %p with %s/%0x/%s", request, preq->network_type,
               preq->network_attrs, preq->network_id);

    return request;
  }

  return NULL;
}

/**
 * Iterate over all requests and call the user given function for each of
 * them
 *
 * @param  fn         the function
 * @param  user_data  user data to pass to the function
 *
 * @return the pointer returned from the user function
 */
gpointer
icd_request_foreach(icd_request_foreach_fn fn, gpointer user_data)
{
  struct icd_context *icd_ctx = icd_context_get();
  GSList *l;

  if (!fn)
  {
    ILOG_ERR("no foreach request function");
    return NULL;
  }

  for (l = icd_ctx->request_list; l; l = l->next)
  {
    if (l->data)
    {
      gpointer rv = fn((struct icd_request *)l->data, user_data);

      if (rv)
        return rv;
    }
    else
      ILOG_ERR("NULL request in foreach function");
  }

  return NULL;
}

/**
 * Find a request
 *
 * @param  network_type   requested network type
 * @param  network_attrs  requested network attributes
 * @param  network_id     requested (meta) IAP name
 *
 * @return the first (and only) request found or NULL
 */
struct icd_request *
icd_request_find(const gchar *network_type, const guint network_attrs,
                 const gchar *network_id)
{
  struct icd_policy_request user_data;

  if (!network_id)
    return NULL;

  user_data.network_type = (gchar *)network_type;
  user_data.network_attrs = network_attrs;
  user_data.network_id = (gchar *)network_id;

  return (struct icd_request *)icd_request_foreach(icd_request_find_foreach,
                                                   &user_data);
}

/**
 * Iterator function for removal by D-Bus sender id
 *
 * @param  request    the request
 * @param  user_data  user data passed to icd_request_tracking_info_delete().
 *
 * @return the request in which the sender id was found or NULL
 */
static gpointer
icd_request_tracking_info_delete_foreach(struct icd_request *request,
                                         gpointer user_data)
{
  GSList *l;
  const char *sender = (const char *)user_data;

  for (l = request->users; l; l = l->next)
  {
    struct icd_tracking_info *track = (struct icd_tracking_info *)l->data;

    if (track)
    {
      if (track->sender && !strcmp(track->sender, sender))
      {
        icd_name_owner_remove_filter(sender);
        request->users = g_slist_remove_link(request->users, l);
        g_slist_free_1(l);
        icd_tracking_info_free(track);
        return request;
      }
    }
    else
      ILOG_ERR("tracking info NULL in request %p", request);
  }

  return NULL;
}

/**
 * Delete a tracked user by D-Bus id
 * @param  sender  the D-Bus sender
 * @return TRUE if the sender was deleted; FALSE on error or sender not found
 */
gboolean
icd_request_tracking_info_delete(const gchar *sender)
{
  if (!sender)
  {
    ILOG_ERR("sender NULL when deleting from request");

    return FALSE;
  }

  return icd_request_foreach(
        icd_request_tracking_info_delete_foreach, (gpointer)sender) != NULL;
}

/**
 * Send a NACK to all D-Bus listeners
 * @param request  the request to NACK
 */
void
icd_request_send_nack(struct icd_request *request)
{
  icd_osso_ic_send_nack(request->users);
  icd_dbus_api_send_nack(request->users, NULL);
  g_slist_free(request->users);
  request->users = NULL;
}

/**
 * Free an icd_request structure
 * @param request  the request
 */
static void
icd_request_free(struct icd_request *request)
{
  struct icd_context *icd_ctx = icd_context_get();

  icd_ctx->request_list = g_slist_remove(icd_ctx->request_list, request);

  if (request->try_iaps)
    ILOG_CRIT("Request %p still has IAPs when free called", request);

  if (request->users)
  {
    ILOG_CRIT("Request %p still has dbus user tracking info when free called",
              request);
  }

  g_free(request->req.service_type);
  g_free(request->req.service_id);
  g_free(request->req.network_type);
  g_free(request->req.network_id);
  g_free(request);
}

/**
 * Notify the caller with the status of the request. The callback will not be
 * called after #ICD_REQUEST_DISCONNECTED or #ICD_REQUEST_DENIED has been
 * reported
 *
 * @param status   the status of the request to pass to the callback
 * @param request  the request whose processing is finished
 */
static void
icd_request_update_status(enum icd_request_status status,
                          struct icd_request *request)
{
  ILOG_DEBUG("request %p with status %s", request,
             icd_request_status_names[status]);

  request->state = status;
}

/**
 * Free memory allocated for all IAPs in a request
 * @param request  the request
 */
void
icd_request_free_iaps(struct icd_request *request)
{
  GSList *l;

  for (l = request->try_iaps; l; request->try_iaps = l)
  {
    struct icd_iap *iap = (struct icd_iap *)l->data;

    if (!iap)
      ILOG_CRIT("IAP in request list is NULL");
    else
      icd_iap_free(iap);

    l = g_slist_delete_link(l, l);
  }
}

/**
 * Cancel a request. The request will be freed when the
 * icd_request_try_iap_cb() callback is called at the time of IAP
 * disconnection
 *
 * @param request       the request
 * @param policy_attrs  ICD_POLICY_ATTRIBUTE_* attributes
 */
void
icd_request_cancel(struct icd_request *request, guint policy_attrs)
{
  struct icd_context *icd_ctx = icd_context_get();

  if (request->try_iaps)
  {
    if (request->state && request->state != ICD_REQUEST_WAITING)
    {
      gint refcount;
      struct icd_iap *iap = (struct icd_iap *)request->try_iaps->data;

      if (policy_attrs & ICD_POLICY_ATTRIBUTE_CONN_UI)
        refcount = -1;
      else
        refcount = g_slist_length(request->users);

      if (!icd_ctx->shutting_down &&
          icd_policy_api_iap_disconnect(&iap->connection, refcount))
      {
        ILOG_INFO("disconnect policy refused to disconnect iap %p", iap);
      }
      else
      {
        icd_status_disconnect(iap, NULL, NULL);
        request->try_iaps = g_slist_remove(request->try_iaps, iap);
        icd_request_free_iaps(request);
        request->try_iaps = g_slist_prepend(request->try_iaps, iap);
        icd_iap_disconnect(iap, NULL);
      }

      return;
    }

    goto cancel_policy_req;
  }

  if (request->state == ICD_REQUEST_POLICY_PENDING ||
      request->state == ICD_REQUEST_WAITING)
  {
cancel_policy_req:
    ILOG_DEBUG("canceling policy request %p, state %d", &request->req,
               request->state);

    icd_policy_api_request_cancel(&request->req);
  }

  icd_request_free_iaps(request);
  icd_request_update_status(ICD_REQUEST_DENIED, request);

  ILOG_INFO("request %p cancelled", request);

  icd_request_free(request);
}

/**
 * Add tracking info to a request
 *
 * @param request  the request
 * @param track    tracking info
 */
void
icd_request_tracking_info_add(struct icd_request *request,
                              struct icd_tracking_info *track)
{
  if (request && track)
  {
    request->users = g_slist_prepend(request->users, track);
    icd_name_owner_add_filter(track->sender);

    ILOG_DEBUG("tracking info sender '%s' and message %p added to request %p",
               track->sender, track->request, request);
  }
}

/**
 * Request a connection. ICd policy will be consulted and any number of IAPs
 * may be created in response.
 *
 * @param  policy_attrs   ICD_POLICY_ATTRIBUTE_* attributes
 * @param  service_type   service type
 * @param  service_attrs  service attributes
 * @param  service_id     service id
 * @param  network_type   network type to connect
 * @param  network_attrs  network attributes
 * @param  network_id     network id uniquely identifies the connection to
 *                        the network module in question
 *
 * @return the newly created request which the caller shall not free or
 *         reference in any way. The pointer is to be passed only to
 *         icd_request_tracking_info_* and icd_request_make functions.
 */
struct icd_request *
icd_request_new(guint policy_attrs, const gchar *service_type,
                const guint service_attrs, const gchar *service_id,
                const gchar *network_type, const guint network_attrs,
                const gchar *network_id)
{
  struct icd_request *request;

  request = g_new0(struct icd_request, 1);
  request->req.attrs = policy_attrs;
  request->req.service_attrs = service_attrs;
  request->req.service_type = g_strdup(service_type);
  request->req.service_id = g_strdup(service_id);
  request->req.network_attrs = network_attrs;
  request->req.network_type = g_strdup(network_type);
  request->req.request_token = request;
  request->req.network_priority = -1;
  request->req.network_id = g_strdup(network_id);

  return request;
}

/**
 * Find out whether the request already exists in the icd context request
 * list
 *
 * @param  request    a request from the list
 * @param  user_data  the request to be added
 *
 * @return the request if it is on the list, NULL if not
 */
static gpointer
icd_request_make_check_duplicate(struct icd_request *request,
                                 gpointer user_data)
{
  if (user_data != request)
    return NULL;

  return request;
}

/**
 * Check policy and try to connect IAP
 * @param  request  the request
 * @return TRUE if connection is being tried; FALSE when there are no more
 *         IAPs to try
 */
static gboolean
icd_request_try_iap(struct icd_request *request)
{
  struct icd_iap *iap;

  while (request->try_iaps)
  {
    icd_request_update_status(ICD_REQUEST_CONNECTING_IAPS, request);
    iap = (struct icd_iap *)request->try_iaps->data;

    ILOG_DEBUG("Trying IAP %p", iap);

    if (iap &&
        icd_policy_api_iap_connect(&iap->connection) == ICD_POLICY_ACCEPTED)
    {
      icd_status_connect(iap, NULL, NULL);
      icd_iap_connect((struct icd_iap *)request->try_iaps->data,
                      icd_request_try_iap_cb, request);
      return TRUE;
    }

    ILOG_INFO("connect policy refused to connect iap %p", iap);
    request->try_iaps = g_slist_remove(request->try_iaps, iap);
    icd_iap_free(iap);
  }

  return FALSE;
}

/**
 * Try to connect the request (for the first time), request UI dialog if
 * unsuccessful
 *
 * @param request  the request to connect
 */
static void
icd_request_connect(struct icd_request *request)
{
  struct icd_request *new_request;

  if (!icd_request_try_iap(request))
  {
    ILOG_INFO("No IAPs created, ask user");

    new_request = icd_request_new(
          ICD_POLICY_ATTRIBUTE_BACKGROUND |
          ICD_POLICY_ATTRIBUTE_CONNECTIONS_FAILED |
          ICD_POLICY_ATTRIBUTE_NO_INTERACTION,
          NULL, 0, NULL, NULL, 0, OSSO_IAP_ASK);
    icd_request_merge(request, new_request);
    icd_request_make(new_request);
  }
}

/**
 * Callback for the new_connection policy request.
 *
 * @param status  status of the policy request
 * @param req     the policy request structure
 */
static void
icd_request_connect_iaps(enum icd_policy_status status,
                         struct icd_policy_request *req)
{
  struct icd_context *icd_ctx = icd_context_get();
  struct icd_request *request;

  if (!req)
  {
    ILOG_CRIT("returned request is NULL after new_request policy modules");
    return;
  }

  request = (struct icd_request *)req->request_token;

  if (icd_ctx->shutting_down || status == ICD_POLICY_REJECTED ||
      status == ICD_POLICY_MERGED )
  {
    ILOG_INFO("final status for new request is %d", status);

    icd_request_update_status(ICD_REQUEST_DENIED, request);
    icd_request_send_nack(request);
    icd_request_free_iaps(request);
    icd_request_free(request);
  }
  else if (status == ICD_POLICY_WAITING)
  {
    ILOG_INFO("request %p is waiting for actions", request);

    icd_request_update_status(ICD_REQUEST_WAITING, request);
  }
  else
  {
    if (g_slist_length(request->try_iaps) > 1)
      request->multi_iaps = TRUE;

    icd_request_connect(request);
  }
}

/**
 * Make a request for a new network connection.
 * @param request  the request - do not use after calling this function, as
 *                 the request may be freed without any further notice
 */
void
icd_request_make(struct icd_request *request)
{
  struct icd_context *icd_ctx = icd_context_get();

  if (icd_ctx->shutting_down)
  {
    ILOG_INFO("no more requests, we're shutting down");
    icd_request_update_status(ICD_REQUEST_DISCONNECTED, request);
    icd_request_send_nack(request);
    icd_request_free_iaps(request);
    icd_request_free(request);
    return;
  }

  ILOG_DEBUG("requesting request %p, attr %0x, %s/%0x/%s,%s/%0x/%s",
             request, request->req.attrs, request->req.service_type,
             request->req.service_attrs, request->req.service_id,
             request->req.network_type, request->req.network_attrs,
             request->req.network_id);

  if (icd_request_foreach(icd_request_make_check_duplicate, request))
  {
    ILOG_DEBUG("icd request %p already exists in list, not adding it twice",
               request);
  }
  else
    icd_ctx->request_list = g_slist_prepend(icd_ctx->request_list, request);

  icd_policy_api_new_request(&request->req, icd_request_connect_iaps, NULL);
}

/**
 * Free all tracking info associated with a request
 * @param request  the request
 */
static void
icd_request_tracking_info_free(struct icd_request *request)
{
  GSList *l;

  for (l = request->users; l; l = l->next)
    icd_tracking_info_free((struct icd_tracking_info *)l->data);

  g_slist_free(request->users);
  request->users = NULL;
}

/**
 * Add a network connection to try.
 *
 * @param request           the request to which the new network is added
 * @param service_type      service provider type, see #srv_provider_api.h
 * @param service_attrs     service provider attributes, see
 *                          #srv_provider_api.h
 * @param service_id        service_provider id, see #srv_provider_api.h
 * @param network_type      network type, see #network_api.h
 * @param network_attrs     network attributes, see #network_api.h
 * @param network_id        network id, see #network_api.h
 * @param network_priority  network priority, default value to use is -1
 */
void
icd_request_add_iap(struct icd_request *request, gchar *service_type,
                    guint service_attrs, gchar *service_id, gchar *network_type,
                    guint network_attrs, gchar *network_id,
                    gint network_priority)
{
  guint attrs = 0;
  struct icd_iap *iap = icd_iap_new();

  request->req.attrs |= ICD_POLICY_ATTRIBUTE_HAS_CONNECTIONS;

  iap->connection.request_token = request;
  iap->connection.service_attrs = service_attrs;
  iap->connection.service_type = g_strdup(service_type);
  iap->connection.service_id = g_strdup(service_id);
  iap->connection.network_type = g_strdup(network_type);
  iap->connection.network_id = g_strdup(network_id);

  if (request->req.attrs & ICD_POLICY_ATTRIBUTE_NO_INTERACTION)
    attrs = ICD_NW_ATTR_SILENT;

  if (request->req.attrs & ICD_POLICY_ATTRIBUTE_ALWAYS_ONLINE)
    attrs |= ICD_NW_ATTR_ALWAYS_ONLINE;

  iap->connection.network_attrs = attrs | network_attrs;

  if (network_priority != -1)
    iap->connection.network_priority = network_priority;
  else
  {
    iap->connection.network_priority =
        icd_network_priority_get(iap->connection.service_type,
                                 iap->connection.service_id,
                                 iap->connection.network_type,
                                 iap->connection.network_attrs);
  }

  icd_iap_id_create(iap, 0);


  ILOG_DEBUG("adding IAP %s/%0x/%s,%s/%0x/%s to request %p",
             iap->connection.service_type,
             iap->connection.service_attrs,
             iap->connection.service_id,
             iap->connection.network_type,
             iap->connection.network_attrs,
             iap->connection.network_id,
             request);

  request->try_iaps = g_slist_append(request->try_iaps, iap);
}

/**
 * Send an ACK to all D-Bus listeners
 *
 * @param request  the request to ack
 * @param iap      the IAP to ack
 */
void
icd_request_send_ack(struct icd_request *request, struct icd_iap *iap)
{
  icd_osso_ic_send_ack(request->users, iap->connection.network_id);

  if (request->state == ICD_REQUEST_DISCONNECTED)
    icd_dbus_api_send_nack(request->users, iap);
  else
    icd_dbus_api_send_ack(request->users, iap);
}

/**
 * Find an iap by module
 *
 * @param  iap        the IAP struct
 * @param  user_data  user data
 *
 * @return TRUE to continue, FALSE to stop iterating
 */
static gboolean
icd_request_find_iap_by_module(struct icd_iap *iap, gpointer user_data)
{
  GSList *l;

  if (!iap || !user_data)
    return TRUE;

  for (l = iap->network_modules; l; l = l->next)
  {
    if (l->data == user_data)
    {
      ILOG_DEBUG("request found matching module %p for iap %p", l->data, iap);
      return FALSE;
    }
  }

  return TRUE;
}

/**
 * Find a request to change to
 *
 * @param  request    the request
 * @param  user_data  user data passed to icd_request_foreach()
 *
 * @return NULL to continue iteration, non-NULL to stop the iteration and
 *         return this pointer in icd_request_foreach().
 */
static gpointer
icd_request_find_changeto(struct icd_request *request, gpointer user_data)
{
  if (request->state == ICD_REQUEST_CHANGETO)
    return request;

  return NULL;
}

/**
 * Callback for requesting retry UI dialog
 *
 * @param success    TRUE if the UI dialog was successfully requested, FALSE
 *                   otherwise
 * @param user_data  the request that was retried
 */
static void
icd_request_retry_cb(gboolean success, gpointer user_data)
{
  if (success)
    ILOG_DEBUG("request dialog successfully requested");
  else
  {
    ILOG_WARN("retry dialog requested from UI returned error");
    icd_request_update_status(ICD_REQUEST_DISCONNECTED,
                              (struct icd_request *)user_data);
    icd_request_send_nack((struct icd_request *)user_data);
    icd_request_free_iaps((struct icd_request *)user_data);
    icd_request_free((struct icd_request *)user_data);
  }
}

/**
 * IAP creation callback. Adds the IAP to the iap context list on success,
 * frees the IAP and tries with a next one if the IAP failed.
 *
 * @param status     status of the IAP creation
 * @param iap        the iap that was tried; the IAP must not be freed in
 *                   this callback
 * @param user_data  the request
 *
 * @todo  generate status updates for the event(s)
 * @todo  how to remove this request if UI goes down
 */
static void
icd_request_try_iap_cb(enum icd_iap_status status, struct icd_iap *iap,
                       gpointer user_data)
{
  struct icd_request *request = (struct icd_request *)user_data;
  struct icd_context *icd_ctx = icd_context_get();
  gboolean success;
  struct icd_iap *iap_blocking;

  request->try_iaps = g_slist_remove(request->try_iaps, iap);

  if (status == ICD_IAP_DISCONNECTED)
  {
    struct icd_request *req;

    icd_request_update_status(ICD_REQUEST_DISCONNECTED, request);
    icd_request_send_ack(request, iap);
    icd_request_tracking_info_free(request);
    icd_policy_api_iap_disconnected(&iap->connection, iap->err_str);
    icd_status_disconnected(iap, 0, iap->err_str);
    icd_request_free_iaps(request);
    icd_iap_free(iap);
    icd_request_free(request);

    req = (struct icd_request *)icd_request_foreach(icd_request_find_changeto,
                                                    NULL);
    if (req)
    {
      ILOG_INFO("request %p was blocked, connecting it", req);
      icd_request_connect(req);
    }
    else
      ILOG_INFO("no other requests blocked");
  }
  else
  {
    if (status == ICD_IAP_DISCONNECTED || status == ICD_IAP_CREATED)
    {
      icd_request_free_iaps(request);
      request->try_iaps = g_slist_prepend(NULL, iap);
      icd_request_update_status(ICD_REQUEST_SUCCEEDED, request);
      icd_request_send_ack(request, iap);
      icd_policy_api_iap_succeeded(&iap->connection);
      icd_status_connected(iap, NULL, NULL);
      return;
    }

    if (status != ICD_IAP_BUSY)
    {
disconnected:
      icd_status_disconnected(iap, NULL, iap->err_str);

      if (request->state == ICD_REQUEST_SUCCEEDED)
      {
        if (!request->try_iaps)
          icd_request_update_status(ICD_REQUEST_DISCONNECTED, request);

        success = TRUE;
      }
      else
        success = FALSE;

      icd_policy_api_iap_disconnected(&iap->connection, iap->err_str);

      if (icd_ctx->shutting_down)
        ILOG_INFO("request %p disconnected, icd2 shutting down", request);
      else
      {
        if (icd_request_try_iap(request))
        {
          icd_iap_free(iap);
          return;
        }

        if (!success &&
            !(request->req.attrs & ICD_POLICY_ATTRIBUTE_NO_INTERACTION) &&
            !iap->user_interaction_done)
        {
          if (request->multi_iaps)
          {
            struct icd_request *req;

            ILOG_DEBUG("No more IAPs to try, requesting user to choose since tried more than one IAP already");
            icd_iap_free(iap);
            req = icd_request_new(ICD_POLICY_ATTRIBUTE_NO_INTERACTION |
                                  ICD_POLICY_ATTRIBUTE_BACKGROUND,
                                  NULL, 0, NULL, NULL, 0, OSSO_IAP_ASK);
            icd_request_merge(request, req);
            icd_request_make(req);
          }
          else
          {
            gchar *err_str;
            gchar *id;

            ILOG_DEBUG("No more IAPs to try, request retry from user");
            request->try_iaps = g_slist_prepend(request->try_iaps, iap);
            icd_request_update_status(ICD_REQUEST_WAITING, request);

            err_str = iap->err_str;

            if (!err_str)
              err_str = ICD_DBUS_ERROR_IAP_NOT_AVAILABLE;

            if (!iap->id || !iap->id_is_local)
              id = iap->connection.network_id;
            else
              id = iap->id;

            icd_osso_ui_send_retry(id, err_str, icd_request_retry_cb, request);
          }

          return;
        }

        ILOG_DEBUG("No more IAPs to try, removing request since %s",
                   !success || iap->user_interaction_done ?
                     "no prompting" : "already connected");
      }

      icd_request_update_status(ICD_REQUEST_DISCONNECTED, request);
      icd_request_send_nack(request);
      icd_request_free_iaps(request);
      icd_request_free(request);
      icd_iap_free(iap);
      return;
    }

    icd_request_update_status(ICD_REQUEST_CHANGETO, request);
    iap_blocking = icd_iap_foreach(icd_request_find_iap_by_module, iap->busy);

    if (!iap_blocking)
    {
      ILOG_WARN("request %p got ICD_IAP_BUSY for iap %p but no blocking iap",
          request, iap);
      goto disconnected;
    }

    ILOG_INFO("request %p waiting for iap %p to close", request, iap_blocking);
    request->try_iaps = g_slist_prepend(request->try_iaps, iap);
    icd_iap_disconnect(iap_blocking, NULL);
  }
}

/**
 * Find a request by IAP id
 *
 * @param iap_id    IAP id
 * @param is_local  TRUE if a locally generated icd2 id is requested, FALSE
 *                  otherwise
 */
struct icd_request *
icd_request_find_by_iap_id(const gchar *iap_id, const gboolean is_local)
{
  struct icd_iap *iap;

  if (iap_id && (iap = icd_iap_find_by_id(iap_id, is_local)))
    return (struct icd_request *)iap->connection.request_token;

  return NULL;
}

/**
 * Find a request by IAP
 *
 * @param  network_type   network type
 * @param  network_attrs  network attributes
 * @param  network_id     network id
 *
 * @return the first (and only) request found or NULL
 */
struct icd_request *
    icd_request_find_by_iap(const gchar *network_type,
                            const guint network_attrs, const gchar *network_id)
{
  struct icd_iap *iap;

  if (network_id &&
      (iap = icd_iap_find(network_type, network_attrs, network_id)))
  {
    return (struct icd_request *)iap->connection.request_token;
  }

  return NULL;
}

/**
 * Remove tracking info to a request
 *
 * @param request  the request
 * @param track    tracking info
 */
void
icd_request_tracking_info_remove(struct icd_request *request,
                                 struct icd_tracking_info *track)
{
  if (request && track)
    request->users = g_slist_remove_all(request->users, track);
}

/**
 * Merge two requets
 *
 * @param  merge_request  request that is to be merged and freed
 * @param  existing       the request to merge with
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_request_merge(struct icd_request *merge_request,
                  struct icd_request *existing)
{
  GSList *l;

  if (merge_request == existing)
  {
    ILOG_CRIT("cannot merge request %p with itself %p", merge_request,
              merge_request);
    return FALSE;
  }

  if (merge_request->state > ICD_REQUEST_WAITING )
  {
    if (merge_request->state == ICD_REQUEST_CONNECTING_IAPS &&
        !merge_request->try_iaps)
    {
      goto skip;
    }

    ILOG_CRIT("Attempted to merge request %p with %p while in state %s with %sIAPs",
              merge_request, existing,
              icd_request_status_names[merge_request->state],
              merge_request->try_iaps ? "" : "no ");
    return FALSE;
  }

  if (merge_request->try_iaps )
  {
    ILOG_WARN("Request %p to merge has IAPs in ICD_REQUEST_POLICY_PENDING_STATE, freeing them. Check policy module order",
              merge_request);
    icd_request_free_iaps(merge_request);
  }

skip:
  l = merge_request->users;

  while (l)
  {
    struct icd_tracking_info *track = (struct icd_tracking_info *)l->data;
    GSList *next = l->next;

    if (!track)
    {
      ILOG_ERR("users list item NULL for request %p", merge_request);
      merge_request->users = g_slist_delete_link(merge_request->users, l);
    }
    else
    {
      if (existing->state == ICD_REQUEST_SUCCEEDED)
      {
        struct icd_tracking_info *merge_track;

        ILOG_DEBUG("copying user '%s' from %p to %p for reference counting purposes",
                   track->sender, merge_request, existing);
        merge_track = icd_tracking_info_new(track->interface, track->sender,
                                            NULL);
        icd_request_tracking_info_add(existing, merge_track);
      }
      else
      {
        ILOG_DEBUG("copying user '%s' and request %p from %p to %p",
                   track->sender, track->request, merge_request, existing);
        icd_request_tracking_info_remove(merge_request, track);
        icd_request_tracking_info_add(existing, track);
      }
    }

    l = next;
  }

  /* FIXME - simplify me */
  existing->req.attrs =
      (merge_request->req.attrs |
       (existing->req.attrs &
        (ICD_POLICY_ATTRIBUTE_CONN_UI |
         ICD_POLICY_ATTRIBUTE_CONNECTIONS_FAILED |
         ICD_POLICY_ATTRIBUTE_ALWAYS_ONLINE_CHANGE))) |
      (existing->req.attrs & (merge_request->req.attrs &
                              (ICD_POLICY_ATTRIBUTE_NO_INTERACTION |
                               ICD_POLICY_ATTRIBUTE_BACKGROUND))) |
      (existing->req.attrs & ICD_POLICY_ATTRIBUTE_HAS_CONNECTIONS);
  icd_policy_api_request_cancel(&merge_request->req);
  icd_request_update_status(ICD_REQUEST_MERGED, merge_request);
  ILOG_DEBUG("Request %p, attrs %0x merged with %p, resulting attrs %0x",
             merge_request, merge_request->req.attrs, existing,
             existing->req.attrs);

  if (existing->state == ICD_REQUEST_SUCCEEDED)
  {
    if (existing->try_iaps && existing->try_iaps->data)
    {
      icd_request_send_ack(merge_request,
                           (struct icd_iap *)existing->try_iaps->data);
      icd_request_tracking_info_free(merge_request);
    }
    else
    {
      ILOG_CRIT("existing ICD_REQUEST_SUCCEEDED request %p has no connection",
                existing);
    }
  }

  icd_request_free(merge_request);

  return TRUE;
}

/** @} */
