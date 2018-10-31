#ifndef ICD_DBUS_H
#define ICD_DBUS_H

#include <glib.h>

#include <dbus/dbus.h>
/* while dbus is under construction, do this */
#include <dbus/dbus-glib-lowlevel.h>

/** Method call handler data */
struct icd_dbus_mcall_table {
  /** method call name */
  const gchar *name;
  /** method call signature */
  const gchar *mcall_sig;
  /** method call reply signature */
  const gchar *reply_sig;
  /** handler function */
  DBusObjectPathMessageFunction handler_fn;
};

/** Signals sent from the interface */
struct icd_dbus_sig_table {
  /** signal name */
  const gchar *name;
  /** signal signature */
  const gchar *mcall_sig;
};

/** Property data */
struct icd_dbus_prop_table {
  /** property name */
  const gchar *name;
};

gboolean icd_dbus_connect_system_path (const char *path,
                                       DBusObjectPathMessageFunction cb,
                                       void *user_data);
gboolean icd_dbus_disconnect_system_path (const char* path);

gboolean icd_dbus_connect_system_bcast_signal (const char *interface,
                                               DBusHandleMessageFunction cb,
                                               void *user_data,
                                               const char *extra_filters);

gboolean icd_dbus_disconnect_system_bcast_signal (const char *interface,
                                                  DBusHandleMessageFunction cb,
                                                  void *user_data,
                                                  const char *extra_filters);

gboolean icd_dbus_register_system_service (const char *path,
                                           const char *service,
                                           guint service_flags,
                                           DBusObjectPathMessageFunction cb,
                                           void *user_data);

void icd_dbus_unregister_system_service (const char *path,
                                         const char *service);

DBusPendingCall *icd_dbus_send_system_mcall (DBusMessage *message,
                                             gint timeout,
                                             DBusPendingCallNotifyFunction cb,
                                             void *user_data);

gboolean icd_dbus_send_system_msg (DBusMessage *message);


/** Callback function for receiving the unique D-Bus id
 * @param name       D-Bus service name
 * @param id         D-Bus id
 * @param user_data  user data
 */
typedef void
(*icd_dbus_get_unique_name_cb_fn) (const gchar *name,
                                   const gchar *id,
                                   gpointer user_data);

void icd_dbus_cancel_unique_name (DBusPendingCall *pending);

gboolean icd_dbus_get_unique_name (const gchar *name,
                                   icd_dbus_get_unique_name_cb_fn cb,
                                   gpointer user_data);

void icd_dbus_close (void);

#endif
