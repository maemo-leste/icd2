/**
@file icd_tracking_info.c

@author patrik.flykt@nokia.com

@copyright (C) 2007 Nokia Corporation. All rights reserved.

@addtogroup icd_tracking_info Tracking of D-Bus users for a request

@ingroup internal

 * @{ */

#include <string.h>
#include "icd_tracking_info.h"
#include "icd_log.h"
#include "icd_context.h"
#include "icd_request.h"

/**
 * Update the D-Bus request message for an existing sender
 *
 * @param  track    tracking info
 * @param  sender   the new sender or NULL to leave unchanged
 * @param  message  the new message to send replies to or NULL to leave
 *                  unchanged
 *
 * @return FALSE if the tracking information still contains a non-acked
 *         message, TRUE on success
 */
gboolean
icd_tracking_info_update(struct icd_tracking_info *track, const gchar *sender,
                         DBusMessage *message)
{
  if (!track)
  {
    ILOG_ERR("tracking info NULL");

    return FALSE;
  }

  if (sender)
  {
    g_free(track->sender);
    track->sender = g_strdup(sender);
  }

  if (!track->request)
  {
    if (message)
    {
      dbus_message_ref(message);
      track->request = message;
    }

    return TRUE;
  }

  ILOG_DEBUG("tracked sender '%s' already has message '%p'", track->sender,
             track->request);

  return FALSE;
}

/**
 * Foreach function for finding a sender
 *
 * @param  request    the request
 * @param  user_data  tracking info
 *
 * @return the request or NULL if not found
 */
static gpointer
icd_tracking_info_foreach(struct icd_request *request, gpointer user_data)
{
  GSList *l;

  for (l = request->users; l; l = l->next)
  {
    struct icd_tracking_info *track = (struct icd_tracking_info *)l->data;

    if (strcmp((const char *)user_data, track->sender))
      return track;

  }

  return NULL;
}

/**
 * Find tracking info based on the sender
 * @param  sender  the D-Bus sender
 * @return tracking info or NULL if not found
 */
struct icd_tracking_info *
icd_tracking_info_find(const gchar *sender)
{
  return (struct icd_tracking_info *)
      icd_request_foreach(icd_tracking_info_foreach, (gpointer)sender);
}

/**
 * Free a tracking info structure
 * @param track  tracking info
 */
void
icd_tracking_info_free(struct icd_tracking_info *track)
{
  if (track)
  {
    g_free(track->sender);

    if (track->request)
      dbus_message_unref(track->request);

    g_free(track);
  }
}

/**
 * Create a new tracking info structure
 *
 * @param  interface  the D-Bus interface this sender is on
 * @param  sender     the D-Bus sender
 * @param  message    optional D-Bus message to send (n)acks to on connect
 *                    and disconnect API calls
 *
 * @return a newly allocated tracking info structure
 */
struct icd_tracking_info *
    icd_tracking_info_new(enum icd_tracking_info_api interface,
                          const gchar *sender, DBusMessage *message)
{
  struct icd_tracking_info *track;

  if (!sender)
    return NULL;

  track = g_new0(struct icd_tracking_info, 1);
  track->interface = interface;
  track->sender = g_strdup(sender);

  if (message)
  {
    dbus_message_ref(message);
    track->request = message;
  }

  return track;
}

/** @} */
