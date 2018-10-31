#ifndef DBUS_API_H
#define DBUS_API_H

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

#ifdef __cplusplus
extern "C" {
#endif

#include <glib.h>

/**
@file dbus_api.h D-Bus API

@addtogroup dbus_api ICd2 D-Bus API

This is an internal API and considered alpha quality. It may change without
further notice.
@{ */

/** D-Bus API interface */
#define ICD_DBUS_API_INTERFACE   "com.nokia.icd2"

/** Well-known path for D-Bus API */
#define ICD_DBUS_API_PATH        "/com/nokia/icd2"

/** flags for #ICD_DBUS_API_SCAN_REQ */
enum icd_scan_request_flags {
  /** request ICd2 to actively scan all networks */
  ICD_SCAN_REQUEST_ACTIVE = 0,
  /** request ICd2 to actively scan saved networks */
  ICD_SCAN_REQUEST_ACTIVE_SAVED = 1,
  /** passively receive scan results when other apps are requesting them;
   * don't start a network scan because of this application; not yet
   * implemented */
  ICD_SCAN_REQUEST_PASSIVE = 2
};

/** Initiate a scan.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_UINT32               passively listen/actively request scan
 *                                results, see #icd_scan_request_flags
 * array of DBUS_TYPE_STRING      network types to scan; no array or empty
 *                                array to scan all network types</pre>
 * Return arguments:
 *<pre>
 * array of DBUS_TYPE_STRING      network types which are going to be scanned</pre>
 */
#define ICD_DBUS_API_SCAN_REQ     "scan_req"

/** Cancel a scan.
 *
 * Arguments:
 *<pre>
 * none</pre>
 * Return arguments:
 *<pre>
 * none</pre>
 */
#define ICD_DBUS_API_SCAN_CANCEL  "scan_cancel_req"

/** status of the scan */
enum icd_scan_status {
  /** the returned network was found */
  ICD_SCAN_NEW = 0,
  /** an existing network with better signal strength is found, applications
   * may want to update any saved data concerning signal strength */
  ICD_SCAN_UPDATE = 1,
  /** other network details have been updated but will not be stored by ICd2;
   * normally networks with this status are best ignored */
  ICD_SCAN_NOTIFY = 2,
  /** the returned network has expired */
  ICD_SCAN_EXPIRE = 3,
  /** this round of scanning is complete and a new scan will be started after
   * the module scan timeout */
  ICD_SCAN_COMPLETE = 4
};

/** Scan results signal.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_UINT32              status, see #icd_scan_status
 * DBUS_TYPE_UINT32              timestamp when last seen
 * DBUS_TYPE_STRING              service type
 * DBUS_TYPE_STRING              service name
 * DBUS_TYPE_UINT32              service attributes, see @ref srv_provider_api
 * DBUS_TYPE_STRING              service id
 * DBUS_TYPE_INT32               service priority within a service type
 * DBUS_TYPE_STRING              network type
 * DBUS_TYPE_STRING              network name
 * DBUS_TYPE_UINT32              network attributes, see @ref network_module_api
 * DBUS_TYPE_ARRAY (BYTE)        network id
 * DBUS_TYPE_INT32               network priority for different network types
 * DBUS_TYPE_INT32               signal strength/quality, 0 (none) - 10 (good)
 * DBUS_TYPE_STRING              station id, e.g. MAC address or similar id
 *                               you can safely ignore this argument
 * DBUS_TYPE_INT32               signal value in dB; use signal strength above
 *                               unless you know what you are doing</pre>
 */
#define ICD_DBUS_API_SCAN_SIG     "scan_result_sig"

/** flags for #ICD_DBUS_API_CONNECT_REQ */
enum icd_connection_flags {
  /** no flags requested */
  ICD_CONNECTION_FLAG_NONE = 0,
  /** requested by application */
  ICD_CONNECTION_FLAG_APPLICATION_EVENT = 0,
  /** requested due to an user event/action */
  ICD_CONNECTION_FLAG_USER_EVENT = 1,

  /** request comes for a connectivity UI; DO NOT use in applications */
  ICD_CONNECTION_FLAG_UI_EVENT = 0x8000
};

/** Request a network connection
 *
 * Make ICd2 select a suitable connection; normally this should be used.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_UINT32              connection flags, see #icd_connection_flags</pre>
 *
 * Make ICd2 try the specified connection(s); use only in special cases.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_UINT32              connection flags, see #icd_connection_flags
 * DBUS_TYPE_ARRAY (
 *   DBUS_TYPE_STRING              service type or empty string
 *   DBUS_TYPE_UINT32              service attributes, see @ref srv_provider_api
 *   DBUS_TYPE_STRING              service id or empty string
 *   DBUS_TYPE_STRING              network type
 *   DBUS_TYPE_UINT32              network attributes, see @ref network_module_api
 *   DBUS_TYPE_ARRAY (BYTE)        network id
 * )</pre>
 *
 * Return arguments:
 *<pre>
 * none</pre>
 */
#define ICD_DBUS_API_CONNECT_REQ "connect_req"

/** Request the 'Select connection' dialog; only connectiviy UIs should be
 * using this function.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_UINT32              connection flags, see #icd_connection_flags</pre>
 *
 * Return arguments:
 *<pre>
 * none</pre>
 */
#define ICD_DBUS_API_SELECT_REQ "select_req"

/** status of the #ICD_DBUS_API_CONNECT_REQ or #ICD_DBUS_API_SELECT_REQ
 * connection requests */
enum icd_connect_status {
  /** the network connection has connected successfully */
  ICD_CONNECTION_SUCCESSFUL = 0,
  /** the network connection did not connect */
  ICD_CONNECTION_NOT_CONNECTED = 1,
  /** the connected network connection was disconnected */
  ICD_CONNECTION_DISCONNECTED = 2
};

/** Connection result signal for #ICD_DBUS_API_CONNECT_REQ and
 * #ICD_DBUS_API_SELECT_REQ requests.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_STRING              service type or empty string
 * DBUS_TYPE_UINT32              service attributes, see @ref srv_provider_api
 * DBUS_TYPE_STRING              service id or empty string
 * DBUS_TYPE_STRING              network type or empty string
 * DBUS_TYPE_UINT32              network attributes, see @ref network_module_api
 * DBUS_TYPE_ARRAY (BYTE)        network id or empty string
 * DBUS_TYPE_UINT32              status, see #icd_connect_status</pre>
 */
#define ICD_DBUS_API_CONNECT_SIG "connect_sig"

/** Request to disconnect an ongoing connection.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_UINT32              connection flags, see #icd_connection_flags
 * DBUS_TYPE_STRING              service type or empty string
 * DBUS_TYPE_UINT32              service attributes, see @ref srv_provider_api
 * DBUS_TYPE_STRING              service id or empty string
 * DBUS_TYPE_STRING              network type or empty string
 * DBUS_TYPE_UINT32              network attributes, see @ref network_module_api
 * DBUS_TYPE_ARRAY (BYTE)        network id or empty string</pre>
 *
 * Request to disconnect the last connection.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_UINT32              connection flags, see #icd_connection_flags</pre>
 *
 * Return arguments:
 *<pre>
 * none</pre>
 */
#define ICD_DBUS_API_DISCONNECT_REQ "disconnect_req"

/** Request state for a connection.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_STRING              service type
 * DBUS_TYPE_UINT32              service attributes, see @ref srv_provider_api
 * DBUS_TYPE_STRING              service id
 * DBUS_TYPE_STRING              network type
 * DBUS_TYPE_UINT32              network attributes, see @ref network_module_api
 * DBUS_TYPE_ARRAY (BYTE)        network id</pre>
 *
 * Return arguments:
 *<pre>
 * DBUS_TYPE_UINT32              number of #ICD_DBUS_API_STATE_SIG signals that
 *                               will be sent</pre>
 *
 * Request state for all connections.
 *
 * Arguments:
 *<pre>
 * none</pre>
 *
 * Return arguments:
 *<pre>
 * DBUS_TYPE_UINT32              number of #ICD_DBUS_API_STATE_SIG signals that
 *                               will be sent</pre>
 */
#define ICD_DBUS_API_STATE_REQ "state_req"

/** Connection state */
enum icd_connection_state {
  /** Network is or became disconnected */
  ICD_STATE_DISCONNECTED = 0,
  /** Establishing network connection */
  ICD_STATE_CONNECTING = 1,
  /** Network is connected */
  ICD_STATE_CONNECTED = 2,
  /** Network is being disconnected */
  ICD_STATE_DISCONNECTING = 3,
  /** service provider module informs about enabled limited connectivity */
  ICD_STATE_LIMITED_CONN_ENABLED = 4,
  /** service provider module informs about disabled limited connectivity */
  ICD_STATE_LIMITED_CONN_DISABLED = 5,
  
  /** Network searching started */
  ICD_STATE_SEARCH_START = 8,
  /** Network searching stopped */
  ICD_STATE_SEARCH_STOP = 9,

  /** Internal network state, IP address(es) has/have been acquired */
  ICD_STATE_INTERNAL_ADDRESS_ACQUIRED = 15
};

/** State signal, sent in response to #ICD_DBUS_API_STATE_REQ or broadcasted
 * whenever the state of a connection changes.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_STRING              service type or empty string
 * DBUS_TYPE_UINT32              service attributes, see @ref srv_provider_api
 * DBUS_TYPE_STRING              service id or empty string
 * DBUS_TYPE_STRING              network type or empty string
 * DBUS_TYPE_UINT32              network attributes, see @ref network_module_api
 * DBUS_TYPE_ARRAY (BYTE)        network id or empty string
 * DBUS_TYPE_STRING              error that occured; empty string on success
 * DBUS_TYPE_UINT32              state of the network connection, see
 *                               #icd_connection_state</pre>
 *
 * State signal, broadcasted at startup if there are no connections ongoing.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_UINT32              #ICD_STATE_DISCONNECTED</pre>
 *
 * State signal, sent in response to #ICD_DBUS_API_STATE_REQ or broadcasted
 * whenever network search begins or ends.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_STRING              the network type for which search is done
 * DBUS_TYPE_UINT32              state of the network connection, see
 *                               #icd_connection_state</pre>
 */
#define ICD_DBUS_API_STATE_SIG "state_sig"

/** Request specific connection statistics.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_STRING              service type
 * DBUS_TYPE_UINT32              service attributes, see @ref srv_provider_api
 * DBUS_TYPE_STRING              service id
 * DBUS_TYPE_STRING              network type
 * DBUS_TYPE_UINT32              network attributes, see @ref network_module_api
 * DBUS_TYPE_ARRAY (BYTE)        network id</pre>
 *
 * Request statistics for all connections.
 *
 * Arguments:
 *<pre>
 * none</pre>
 *
 * Return arguments:
 *<pre>
 * DBUS_TYPE_UINT32              number of #ICD_DBUS_API_STATISTICS_SIG sent,
 *                               zero if no connections are ongoing</pre>
 */
#define ICD_DBUS_API_STATISTICS_REQ "statistics_req"

/** Statistics signal, sent in response to #ICD_DBUS_API_STATISTICS_REQ if
 * there are ongoing connections.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_STRING              service type or empty string
 * DBUS_TYPE_UINT32              service attributes, see @ref srv_provider_api
 * DBUS_TYPE_STRING              service id or empty string
 * DBUS_TYPE_STRING              network type or empty string
 * DBUS_TYPE_UINT32              network attributes, see @ref network_module_api
 * DBUS_TYPE_ARRAY (BYTE)        network id or empty string
 * DBUS_TYPE_UINT32              time active, measured in seconds
 * DBUS_TYPE_INT32               signal strength/quality, see #icd_nw_levels
 * DBUS_TYPE_UINT32              bytes sent
 * DBUS_TYPE_UINT32              bytes received</pre>
 */
#define ICD_DBUS_API_STATISTICS_SIG "statistics_sig"

/** Request specific connection address info. Note that the address
 * information returned is what was assigned to the connection, any VPNs or
 * tunnels set up later will not get reported.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_STRING              service type
 * DBUS_TYPE_UINT32              service attributes, see @ref srv_provider_api
 * DBUS_TYPE_STRING              service id
 * DBUS_TYPE_STRING              network type
 * DBUS_TYPE_UINT32              network attributes, see @ref network_module_api
 * DBUS_TYPE_ARRAY (BYTE)        network id</pre>
 *
 * Request address info for all connections.
 *
 * Arguments:
 *<pre>
 * none</pre>
 *
 * Return arguments:
 *<pre>
 * DBUS_TYPE_UINT32              number of #ICD_DBUS_API_ADDRINFO_SIG sent,
 *                               zero if no connections are ongoing</pre>
 *
 * Note that the address information returned is what was assigned to the
 * connection, any VPNs or tunnels set up later will not get reported.
 */
#define ICD_DBUS_API_ADDRINFO_REQ "addrinfo_req"

/** Address info signal, sent in response to #ICD_DBUS_API_ADDRINFO_REQ if
 * there are ongoing connections.
 *
 * Arguments:
 *<pre>
 * DBUS_TYPE_STRING              service type or empty string
 * DBUS_TYPE_UINT32              service attributes, see @ref srv_provider_api
 * DBUS_TYPE_STRING              service id or empty string
 * DBUS_TYPE_STRING              network type or empty string
 * DBUS_TYPE_UINT32              network attributes, see @ref network_module_api
 * DBUS_TYPE_ARRAY (BYTE)        network id or empty string
 * DBUS_TYPE_ARRAY (
 *   DBUS_TYPE_STRING              IP address
 *   DBUS_TYPE_STRING              IP netmask
 *   DBUS_TYPE_STRING              IP default gateway
 *   DBUS_TYPE_STRING              IP address of DNS server #1
 *   DBUS_TYPE_STRING              IP address of DNS server #2
 *   DBUS_TYPE_STRING              IP address of DNS server #3
 * )</pre>
 */
#define ICD_DBUS_API_ADDRINFO_SIG "addrinfo_sig"

/** @} */

#ifdef __cplusplus
}
#endif

#endif
