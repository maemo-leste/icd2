#!/bin/sh

# Copyright (C) 2007,2008 Nokia Corporation. All rights reserved.
# Author: patrik.flykt@nokia.com

icd_entry=system/osso/connectivity/IAP

# Run gconftool via server or directly if D-Bus is not running
gconftool_run () {
    if gconftool-2 -p &>/dev/null
    then
	gconftool-2 $* 2>/dev/null
    else
	gconftool-2 --direct \
	    --config-source xml::/etc/gconf/gconf.xml.defaults \
	    $* | sed '1 d' 2>/dev/null
    fi
}

# Read old auto connect gconf entry
read_auto_connect () {
    gconftool_run --ignore-schema-defaults \
	-g /$icd_entry/auto_connect \
	2>/dev/null
}

# Remove old auto connect gconf entry
remove_auto_connect () {
    gconftool_run --unset /$icd_entry/auto_connect
}

# Set the new auto connect gconf entry
# $1: gconf list of network types to set
set_auto_connect () {
    test x$1 == x && return

    gconftool_run -s /system/osso/connectivity/network_type/auto_connect \
	-t list --list-type string "$*"
}

# check whether there was something backed up or force run if no arguments
if test x"$1" != x
then
    grep $icd_entry "$1" &>/dev/null || exit 0
fi

type_wlan="WLAN_INFRA,WLAN_ADHOC"
type_dun="DUN_GSM_PS,DUN_CDMA_PSD,DUN_GSM_CS,DUN_CDMA_QNC,DUN_CDMA_CSD"
type_wimax="WIMAX"

case `read_auto_connect` in
    "Phone")
	network_types="[$type_dun]"
	;;
    "WLAN")
	network_types="[$type_wlan]"
	;;
    "WIMAX")
	network_types="[$type_wimax]"
	;;
    "WIMAX,WLAN")
	network_types="[$type_wlan,$type_wimax]"
	;;
    "Any")
	network_types="[$type_wlan,$type_wimax,$type_dun]"
	;;
    "None")
	network_types="[]"
	;;
	*)
	;;
esac

set_auto_connect "$network_types"
remove_auto_connect

exit 0
