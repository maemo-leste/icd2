#ifndef NETWORK_API_DEFINES_H
#define NETWORK_API_DEFINES_H

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

/**
@file network_api_defines.h

This file contains general network API definitions.

@addtogroup network_module_api Network module API definitions

 * @{ */

/** status of the #icd_nw_api function call returned in respective callbacks */
enum icd_nw_status {
  /** function call succeeded; ICd will now call the next non-NULL
   * #icd_nw_api '_up' function on the same layer from any other module that
   * has the same type but greater priority number */
  ICD_NW_SUCCESS = 0,
  /** function call succeeded; ICd will now call the next non-NULL
   * #icd_nw_api '_up' function from the layer above. This status code is
   * ignored on disconnect. */
  ICD_NW_SUCCESS_NEXT_LAYER = 1,
  /** restart this IAP; ICd will first call the all #icd_nw_api '_down'
   * functions starting with the one for this level and then restart from
   * link_up. This status code is ignored on disconnect. */
  ICD_NW_RESTART = 2,
  /** generic error condition; the network will be closed down and the
   * corresponding 'err_str' should contain more information on the error.
   * The 'err_str' is currently formatted as a D-Bus error, e.g.
   * "com.nokia.icd.error.somedescriptivestring". This status code is ignored
   * on disconnect. */
  ICD_NW_ERROR = 3,
  /** network module is already in use by another connection. This status
   * code is ignored on disconnect. */
  ICD_NW_TOO_MANY_CONNECTIONS = 4,
  /** network module has done all the needed user interaction, no need to
   * show retry dialogs or similar. This status code is ignored on
   * disconnect. */
  ICD_NW_ERROR_USER_ACTION_DONE = 5,
  /** Restart the IP layer of this IAP; ICd will first call the
   * icd_srv_disconnect_fn if needed and then call any required
   * icd_nw_ip_down_fn functions. When done, any required icd_nw_ip_up and
   * icd_srv_connect_fn functions will be called normally. This status code
   * is ignored on disconnection. */
  ICD_NW_RESTART_IP = 6,
  /** Restart up to link post layer of this IAP; works similarly to
   * #ICD_NW_RESTART_IP. This status code is ignored on disconnect. */
  ICD_NW_RESTART_LINK_POST = 7,
  /** Restart up to link layer of this IAP; works similarly to
   * #ICD_NW_RESTART_IP. This status code is ignored on disconnect. */
  ICD_NW_RESTART_LINK = 8
};

/** Renew status codes */
enum icd_nw_renew_status {
  /** No visible changes in any network parameters changed, no further action
   * required */
  ICD_NW_RENEW_NO_CHANGES = 0,
  /** Network parameters have changed, restart this and above network layers */
  ICD_NW_RENEW_CHANGES_MADE = 1
};

/** signal level from lowest (_NONE) to highest (_10) */
enum icd_nw_levels {
  /** no signal */
  ICD_NW_LEVEL_NONE = 0,
  /** signal level 1 */
  ICD_NW_LEVEL_1,
  /** signal level 2 */
  ICD_NW_LEVEL_2,
  /** signal level 3 */
  ICD_NW_LEVEL_3,
  /** signal level 4 */
  ICD_NW_LEVEL_4,
  /** signal level 5 */
  ICD_NW_LEVEL_5,
  /** signal level 6 */
  ICD_NW_LEVEL_6,
  /** signal level 7 */
  ICD_NW_LEVEL_7,
  /** signal level 8 */
  ICD_NW_LEVEL_8,
  /** signal level 9 */
  ICD_NW_LEVEL_9,
  /** signal level 10 */
  ICD_NW_LEVEL_10
};


/** Type of network id; set for IAP name, unset for local id, e.g. WLAN SSID */
#define ICD_NW_ATTR_IAPNAME        0x01000000
/** UI and user interaction forbidden if set, allowed if unset */
#define ICD_NW_ATTR_SILENT         0x02000000
/** Whether we have all required credentials to authenticate ourselves to the
 * network automatically without any user interaction */
#define ICD_NW_ATTR_AUTOCONNECT    0x04000000
/** Whether this network always needs service provider support in order to
 * get connected */
#define ICD_NW_ATTR_SRV_PROVIDER   0x10000000
/** Whether the connection attempt is done because of always online policy,
 * manual connection attempts do not set this */
#define ICD_NW_ATTR_ALWAYS_ONLINE  0x20000000
/** Mask for network attribute local values, e.g. security settings, WLAN
 * mode, etc. These values might be evaluated by relevant UI components */
#define ICD_NW_ATTR_LOCALMASK      0x00FFFFFF


/** search function callback status */
enum icd_network_search_status {
  /** Search continues, more values to be expected soon */
  ICD_NW_SEARCH_CONTINUE = 0,
  /** Search is completed or the module cannot continue due to some error;
   * the search callback values are ignored */
  ICD_NW_SEARCH_COMPLETE = 1,
  /** Search result is expired due to external reasons and needs to be
   * removed immediately from the cache */
  ICD_NW_SEARCH_EXPIRE = 2
};

/** Search for all available networks in range */
#define  ICD_NW_SEARCH_SCOPE_ALL   0x0
/** Search for saved IAPs */
#define  ICD_NW_SEARCH_SCOPE_SAVED 0x1

/** Network module layer */
enum icd_nw_layer {
  /** No layer */
  ICD_NW_LAYER_NONE = 0,
  /** Link layer */
  ICD_NW_LAYER_LINK = 1,
  /** Post-link or link authentication layer */
  ICD_NW_LAYER_LINK_POST = 2,
  /** IP network layer */
  ICD_NW_LAYER_IP = 3,
  /** Service layer provided by a service module, see @ref srv_provider_api */
  ICD_NW_LAYER_SERVICE = 4,
  /** All layers */
  ICD_NW_LAYER_ALL = 5
};

/** @} */

#ifdef __cplusplus
}
#endif

#endif
