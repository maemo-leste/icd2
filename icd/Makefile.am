sbin_PROGRAMS = icd2

icd2_CFLAGS = -I../support $(ICD_CFLAGS) -Wall
icd2_LDFLAGS = -export-dynamic
icd2_LDADD = $(ICD_LIBS) ../support/libicd_log.la ../support/libicd_dbus.la -ldl

icd2_SOURCES =	icd_args.c \
		icd_context.c \
		icd_pid.c \
		icd_signal.c \
		icd_plugin.c \
		icd_exec.c \
		icd_idle_timer.c \
		icd_iap.c \
		icd_script.c \
		icd_network_api.c \
		icd_scan.c \
		icd_type_modules.c \
		icd_dbus_api.c \
		icd_request.c \
		icd_name_owner.c \
		icd_tracking_info.c \
		icd_srv_provider.c \
		icd_gconf.c \
		icd_osso_ic.c \
		icd_status.c \
		icd_network_priority.c \
		icd_policy_api.c \
		icd_wlan_defs.c \
		icd_version.c

MAINTAINERCLEANFILES = Makefile.in
