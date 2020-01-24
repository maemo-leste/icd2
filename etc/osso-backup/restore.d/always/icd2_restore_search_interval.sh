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

# Read old search interval gconf entry
read_search_interval () {
    gconftool_run --ignore-schema-defaults \
	-g /$icd_entry/search_interval \
	2>/dev/null
}

# Remove old search interval gconf entry
remove_search_interval () {
    gconftool_run --unset /$icd_entry/search_interval
}

# Set the new search interval gconf entry
# $1: gconf list of network types to set
set_search_interval () {
    test x$1 == x && return

    gconftool_run -s /system/osso/connectivity/network_type/search_interval \
	-t int "$1"
}

# check whether there was something backed up or force run if no arguments
if test x"$1" != x
then
    grep $icd_entry "$1" &>/dev/null || exit 0
fi

timeout=`read_search_interval`
if test x"$timeout" != x
    then
    set_search_interval `expr "$timeout" \* 60`
fi
remove_search_interval

exit 0
