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

# Read idle timer setting
# $1: idle timer to read
read_idle_timer() {
    test x$1 == x && return
    gconftool_run --ignore-schema-defaults \
	-g /$icd_entry/"$1" 2> /dev/null
}

# Remove old idle timer gconf entry
# $1: idle timer to read
remove_idle_timer() {
    test x$1 == x && return
    gconftool_run --unset /$icd_entry/"$1" 2>/dev/null
}

# Set idle timer value
# $1: idle timer value
# $2 [$3 [$4... ]]: network type the idle timer applies to
set_idle_timer() {
    test x$1 == x && return

    timeout=`expr "$1" \* 60`
    shift

    while test x$1 != x
    do
      gconftool_run \
	  -s /system/osso/connectivity/network_type/"$1"/idle_timeout \
	  -t int "$timeout"
      shift
    done
}

# check whether there was something backed up or force run if no arguments
if test x"$1" != x
then
    grep $icd_entry "$1" &>/dev/null || exit 0
fi

timeout=`read_idle_timer timeout_wlan`
if test x"$timeout" != x
then
    set_idle_timer "$timeout" WLAN_INFRA WLAN_ADHOC
fi

timeout=`read_idle_timer timeout_dun_ps`
if test x"$timeout" != x
    then
    set_idle_timer "$timeout" DUN_GSM_PS DUN_CDMA_PSD
fi

timeout=`read_idle_timer timeout_dun_cs`
if test x"$timeout" != x
    then
    set_idle_timer "$timeout" DUN_GSM_CS DUN_CDMA_QNC DUN_CDMA_CSD
fi

remove_idle_timer timeout_wlan
remove_idle_timer timeout_dun_ps
remove_idle_timer timeout_dun_cs

exit 0
