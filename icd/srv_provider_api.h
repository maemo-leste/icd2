#ifndef SRV_PROVIDER_API_H
#define SRV_PROVIDER_API_H

/*
 This file is part of icd2-dev, ICd2 development header files.

 Copyright (C) 2007-2008 Nokia Corporation. All rights reserved.

 Contact: Patrik Flykt <patrik.flykt@nokia.com>

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.
  * Neither the name of Nokia Corporation nor the names of its contributors
    may be used to endorse or promote products derived from this software
    without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 POSSIBILITY OF SUCH DAMAGE.
*/

/**
@file srv_provider_api.h Service provider API

@addtogroup srv_provider_api Service Provider API

The service provider API is considered beta quality, it may still be modified
at some point.

The service provider API makes it possible to run tasks after an IP address
has been acquired in the network module ip layer but before the network
is announced to applications as being connected. The settings for a service
module include the network types the service module is interested in, whereby
the icd_srv_identify_fn() is called whenever there is a scan result available
with a matching network type. In this way e.g. hotspot WLAN networks can
be supported, since they need IP layer connectivity in order to reach an
authentication service that usually means posting credentials on a web page.
<p>
In order for the network connection with an associated service module to
succeed, icd_srv_connect_cb_fn() needs to be called with an #ICD_SRV_SUCCESS
status code. On error conditions or when closing the network connection the
optional icd_srv_disconnect_fn() function is called, whereafter the network
is disconnected as described in @ref network_module_api.
<p>
The following service module settings are located at the gconf path
<code>/system/osso/connectivity/srv_provider/</code><i>&lt;service type
name&gt;</i>:
<ul>
<li><code>module</code> Name of the shared service provider module that
implements this service.
<li><code>network_type</code> A list of strings where each string
contains the name of the network types this service is interested in.
</ul>
<p>

 * @{ */

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>
#include <unistd.h>
#include <glib.h>

#include <dbus_api.h>
#include <network_api.h>

/** Service provider module version equal to the network module version */
#define ICD_SRV_MODULE_VERSION ICD_NW_MODULE_VERSION

/** status of the icd_srv_api function call returned in the callback */
enum icd_srv_status {
  /** service provider (authentication) functionality succeeded; network
   * connection is functioning */
  ICD_SRV_SUCCESS = 0,
  /** restart this IAP; ICd will call all icd_nw_api '_down' functions for
   * the associated network module and then restart from link_up all the way
   * to the service API */
  ICD_SRV_RESTART = 1,
  /** error; the IAP will be disconnected */
  ICD_SRV_ERROR = 2
};

/** status of the network identification, use OR to add parts together */
enum icd_srv_identify_status {
  /** the network is not identified by this service provider */
  ICD_SRV_UNKNOWN = 0,
  /** the network is identified to be used by this service provider */
  ICD_SRV_IDENTIFIED = 1,
  /** whether there will be more identify results to come */
  ICD_SRV_CONTINUE = 8,
};

/** Service identification callback.
 * @param status             status of the identification procedure
 * @param service_type       service type
 * @param service_name       name of the service provider displayable to the
 *                           user/UI
 * @param service_attrs      service attributes
 * @param service_id         internal service id
 * @param service_priority   priority within a service type
 * @param network_type       network type that was identified
 * @param network_attrs      network attributes that were identified
 * @param network_id         network id that was identified
 * @param identify_cb_token  the token passed to the identification function
 */
typedef void
(*icd_srv_identify_cb_fn) (const enum icd_srv_identify_status status,
 			   const gchar *service_type,
			   const gchar *service_name,
			   const guint service_attrs,
			   const gchar *service_id,
			   const gint service_priority,
			   const gchar *network_type,
			   const guint network_attrs,
			   const gchar *network_id,
			   gpointer identify_cb_token);
/** Identify whether a given network is usable with this service provider.
 * Even though this function has a callback, it is important that the
 * decision is made as fast as possible in order not to slow down network
 * scan processing. The service provider and network modules have to have a
 * common understanding of both network_attrs and network_id parameters.
 *
 * @param status             status, see #icd_scan_status
 * @param network_type       network type
 * @param network_name       name of the network displayable to the user
 * @param network_attrs      network attributes
 * @param network_id         network identification
 * @param signal             signal strength
 * @param station_id         station id, e.g. MAC address or similar id
 * @param dB                 absolute signal strength value in dB
 * @param identify_cb        callback to call when the identification has
 *                           been done
 * @param identify_cb_token  token to pass to the identification callback
 */
typedef void (*icd_srv_identify_fn) (enum icd_scan_status status,
				     const gchar *network_type,
				     const gchar *network_name,
				     const guint network_attrs,
				     const gchar *network_id,
				     const guint network_priority,
				     enum icd_nw_levels signal,
				     const gchar *station_id,
				     const gint dB,
				     icd_srv_identify_cb_fn identify_cb,
				     gpointer identify_cb_token,
				     gpointer *private);

/** Disconnect callback for the service provider module
 * @param status               status of the disconnect, ignored for now
 * @param disconnect_cb_token  token passed to the disconnect function
 */
typedef void (*icd_srv_disconnect_cb_fn) (enum icd_srv_status status,
					  gpointer disconnect_cb_token);
/** Disconnect function for the service provider module
 * @param service_type         service type
 * @param service_attrs        service attributes
 * @param service_id           internal id identifying the service
 * @param network_type         type of network connected to
 * @param network_attrs        network attributes
 * @param network_id           network identification
 * @param interface_name       network interface used
 * @param disconnect_cb        callback to call when disconnection is
 *                             completed
 * @param disconnect_cb_token  token to pass to the callback
 * @param private              reference to the private icd_srv_api member
 */
typedef void (*icd_srv_disconnect_fn) (const gchar *service_type,
				       const guint service_attrs,
				       const gchar *service_id,
				       const gchar *network_type,
				       const guint network_attrs,
				       const gchar *network_id,
				       const gchar *interface_name,
				       icd_srv_disconnect_cb_fn disconnect_cb,
				       gpointer disconnect_cb_token,
				       gpointer *private);

/** Connect callback for the service provider module
 * @param status            status of the connect attempt; with
 *                          ICD_SRV_RESTART the IAP will be disconnected and
 *                          reconnected again
 * @param err_str           error string or NULL on success
 * @param connect_cb_token  token passed to the connect function
 */
typedef void (*icd_srv_connect_cb_fn) (enum icd_srv_status status,
				       const gchar *err_str,
				       gpointer connect_cb_token);
/** Connect (or authenticate) with a service provider.
 * @param service_type      service type
 * @param service_attrs     service attributes
 * @param service_id        internal id identifying the service
 * @param network_type      type of network connected to
 * @param network_attrs     network attributes
 * @param network_id        network identification
 * @param interface_name    network interface used
 * @param connect_cb        callback to call when connection attempt is
 *                          completed
 * @param connect_cb_token  token to pass to the callback
 * @param private           reference to the private icd_srv_api member
 */
typedef void (*icd_srv_connect_fn) (const gchar *service_type,
				    const guint service_attrs,
				    const gchar *service_id,
				    const gchar *network_type,
				    const guint network_attrs,
				    const gchar *network_id,
				    const gchar *interface_name,
				    icd_srv_connect_cb_fn connect_cb,
				    gpointer connect_cb_token,
				    gpointer *private);

/** Notification function for child process termination
 * @param pid         the process id that exited
 * @param exit_value  process exit value
 * @param private     a reference to the icd_nw_api private member
 */
typedef void (*icd_srv_child_exit_fn) (const pid_t pid,
				       const gint exit_status,
				       gpointer *private);

/** Destruction function that cleans up after the module. The list of network
 * and service types in the icd_srv_api structure is deleted by ICd. The
 * destruction function will not be called before all child processes have
 * exited.
 *
 * @param private  a reference to the icd_nw_api private member
 */
typedef void (*icd_srv_destruct_fn) (gpointer *private);

/** icd_srv_api defines the service provider functions implemented by the
 * module */
struct icd_srv_api {

  /** ICd2 version this module is compiled against, set to
   * #ICD_SRV_MODULE_VERSION */
  const gchar *version;

  /** private data usable by the module */
  gpointer private;

  /** connect/authenticate with the service provider */
  icd_srv_connect_fn connect;

  /** connect/deauthenticate with the service provider */
  icd_srv_disconnect_fn disconnect;

  /** network identification function */
  icd_srv_identify_fn identify;

  /** child process exit notification */
  icd_srv_child_exit_fn child_exit;

  /** cleanup function */
  icd_srv_destruct_fn srv_destruct;
};


/** Prototype function for notifying ICd that a child process has been
 * started. The network destruction function will not be called before all
 * child processes have exited.
 *
 * @param pid             process id
 * @param watch_cb_token  the watch callback token given on initialization
 */
typedef void (*icd_srv_watch_pid_fn) (const pid_t pid,
				      gpointer watch_cb_token);

/** Prototype for the module to request closing down its connection due to
 * internal or external events.
 *
 * @param status         reason for closing; #ICD_SRV_RESTART if the
 *                       connection needs to be restarted, success or error
 *                       will both close the network connection
 * @param err_str        NULL if the service provisioning was disconnected
 *                       normally or any ICD_DBUS_ERROR_* from osso-ic-dbus.h
 *                       on error
 * @param service_type   the service type
 * @param service_attrs  attributes
 * @param service_id     internal service id
 * @param network_type   the network type
 * @param network_attrs  network attributes
 * @param network_id     network id
 */
typedef void (*icd_srv_close_fn) (enum icd_srv_status status,
				  const gchar *err_str,
				  const gchar *service_type,
				  const guint service_attrs,
				  const gchar *service_id,
				  const gchar *network_type,
				  const guint network_attrs,
				  const gchar *network_id);

/** Enabled/disabled limited connectivity status values */
enum icd_srv_limited_conn_status {
  /** Limited connectivity is disabled */
  ICD_SRV_LIMITED_CONN_DISABLED = 0,
  /** Limited connectivity is enabled */
  ICD_SRV_LIMITED_CONN_ENABLED = 1
};
/** Inform about enabled or disabled limited connectivity for service
 * providing purposes. An example of limited connectivity is a network that
 * allows IP connections only to certain authenticaion/log in server until
 * the service module has finished its task. It is fully optional to use this
 * function and full connectivity will be signalled when the network is
 * connected.
 *
 * @param conn_status    whether limited connectivity is enabled or disabled
 * @param service_type   the service type
 * @param service_attrs  attributes
 * @param service_id     internal service id
 * @param network_type   the network type
 * @param network_attrs  network attributes
 * @param network_id     network id
 */
typedef void
(*icd_srv_limited_conn_fn) (const enum icd_srv_limited_conn_status conn_status,
			    const gchar *service_type,
			    const guint service_attrs,
			    const gchar *service_id,
			    const gchar *network_type,
			    const guint network_attrs,
			    const gchar *network_id);

/** Prototype for the service api initialization function. ICd searches each
 * library for an instance of this function prototype called 'icd_srv_init'.
 *
 * @param  srv_api         icd_srv_api structure to be filled in by the
 *                         module
 * @param  watch_cb        function to inform ICd that a child process is to
 *                         be monitored for exit status
 * @param  watch_cb_token  token to pass to the pid watch function
 * @param  close_cb        function to inform ICd that the network connection
 *                         is to be closed
 * @return TRUE on success, FALSE on failure whereby the module will be
 *         immediately unloaded
 */
typedef gboolean (*icd_srv_init_fn) (struct icd_srv_api *srv_api,
				     icd_srv_watch_pid_fn watch_cb,
				     gpointer watch_cb_token,
				     icd_srv_close_fn close,
				     icd_srv_limited_conn_fn limited_conn);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
