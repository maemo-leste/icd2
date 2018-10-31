#include <gconf/gconf-client.h>
#include <osso-ic-gconf.h>

#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "icd_log.h"
#include "icd_idle_timer.h"
#include "icd_context.h"

#define ICD2_IDLE_TIMER_IPTABLES_PREFIX "icd2-idle-"

#define IPTABLES_EXEC(...) \
({ \
  gboolean rv = FALSE; \
  pid_t pid = fork(); \
  if (pid != -1) { \
    if (!pid) {\
      execl("/sbin/iptables", "iptables", __VA_ARGS__, NULL); \
      exit(1); \
    } \
    waitpid(pid, NULL, 0); \
    rv = TRUE; \
  } \
  if (rv) { \
    pid = fork(); \
    if (pid != -1) { \
      if (!pid) {\
        execl("/sbin/ip6tables", "ip6tables", __VA_ARGS__, NULL); \
        exit(1); \
      } \
      waitpid(pid, NULL, 0); \
    } \
    else \
      rv = FALSE; \
  } \
  rv; \
})

/**
 * Unset idletimer iptables rules
 * @param  interface  the interface name
 * @return TRUE on success, FALSE on failure
 */
static gboolean
icd_idle_timer_unset_rules (const gchar *interface)
{
  gchar *chain = g_strconcat(ICD2_IDLE_TIMER_IPTABLES_PREFIX, interface, NULL);
  gboolean rv =
      IPTABLES_EXEC("-D", "OUTPUT", "-o", interface, "-j", chain) &&
      IPTABLES_EXEC("-F", chain) &&
      IPTABLES_EXEC("-X", chain);

  g_free(chain);

  if (rv)
    ILOG_INFO("idle timer unset timeout for interface '%s'", interface);
  else
    ILOG_ERR("idle timer unset fork failed");

  return rv;
}

/**
 * Unset idle timer for an IAP
 * @param  iap  the IAP
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_idle_timer_unset(struct icd_iap *iap)
{
  if (iap->idletimer_id)
  {
    g_source_remove(iap->idletimer_id);
    iap->idletimer_id = 0;
    ILOG_DEBUG("idle timer removed iap %p idle timer id", iap);
  }

  if (!iap->interface_name)
  {
    ILOG_ERR("idle timer iap %p interface is NULL", iap);
    return FALSE;
  }

  return icd_idle_timer_unset_rules(iap->interface_name);
}

/**
 * Set idletimer iptables rules
 *
 * @param  interface    the interface name
 * @param  timeout_str  time in seconds to wait until interface is considered
 *                      idle
 *
 * @return TRUE on success, FALSE on failure
 */
static gboolean
icd_idle_timer_set_rules (const gchar *interface, const gchar *timeout_str)
{
  gchar *chain = g_strconcat("icd2-idle-", interface, NULL);
  gboolean ipt_exec_res =
      IPTABLES_EXEC("-N", chain) &&
      IPTABLES_EXEC("-F", chain) &&
      IPTABLES_EXEC("-A", chain, "-p", "udp", "--sport", "68", "--dport", "67",
                    "-j", "RETURN") &&
      IPTABLES_EXEC("-A", chain, "-p", "icmp", "-j", "RETURN") &&
      IPTABLES_EXEC("-A", chain, "-j", "IDLETIMER", "--timeout",
                    timeout_str) &&
      IPTABLES_EXEC("-I", "OUTPUT", "-o", interface, "-j", chain);

  g_free(chain);

  if (ipt_exec_res)
  {
    gchar *sysfs_path =
        g_strdup_printf("/sys/class/net/%s/idletimer", interface);
    int fd = open(sysfs_path, O_WRONLY);

    if (fd < 0)
      ILOG_ERR("idle timer could not open '%s'", sysfs_path);
    else
    {
      if (write(fd, timeout_str, strlen(timeout_str)) == strlen(timeout_str))
      {
        g_free(sysfs_path);
        close(fd);

        return TRUE;
      }
      else
      {
        ILOG_ERR("idle timer could not write to '%s'", sysfs_path);
        close(fd);
      }

      g_free(sysfs_path);
    }
  }
  else
    ILOG_ERR("idle timer fork failed");

  ILOG_ERR("idle timer failed to set up rules");

  return FALSE;
}

/**
 * Idle timer callback function called when the idle timer has triggered
 *
 * @param  source     the GIOChannel event source
 * @param  condition  the condition which has been satisfied
 * @param  data       user data set in g_io_add_watch() or
 *                    g_io_add_watch_full()
 *
 * @return TRUE to continue watching idle timer, FALSE to stop
 */
static gboolean
icd_idle_timer_trigger(GIOChannel *source, GIOCondition condition,
                       gpointer data)
{
  struct icd_iap *iap = (struct icd_iap *)data;
  guint secs;
  gchar buf[16];
  gsize bytes_read;

  if (g_io_channel_read_chars(source, buf, sizeof(buf) - 1, &bytes_read, NULL))
  {
    ILOG_INFO("idle timer failed to read event source");
    return TRUE;
  }

  buf[bytes_read] = 0;
  secs = strtol(buf, NULL, 10);

  ILOG_DEBUG("idle timer triggered for iap %p at %d secs", iap, secs);

  if (secs)
    return TRUE;

  ILOG_INFO("idle timer shutting down iap %p", iap);
  iap->idletimer_id = 0;
  icd_iap_disconnect(iap, NULL);

  return FALSE;
}

/**
 * Start idle timer
 *
 * @param  iap      the IAP
 * @param  timeout  timeout in seconds
 *
 * @return TRUE on successful starting of the idle timer; FALSE on error
 */
static gboolean
icd_idle_timer_start(struct icd_iap *iap, guint timeout)
{
  gchar *sysfs_path;
  GIOChannel *io;
  gchar *timeout_str;

  if (!timeout)
    return TRUE;

  if (iap->idletimer_id)
  {
    ILOG_INFO("idle timer set while idle timer exists");
    g_source_remove(iap->idletimer_id);
    iap->idletimer_id = 0;
  }

  timeout_str = g_strdup_printf("%u", timeout);

  if (!icd_idle_timer_set_rules(iap->interface_name, timeout_str))
  {
    g_free(timeout_str);
    return FALSE;
  }

  g_free(timeout_str);

  sysfs_path = g_strdup_printf("/sys/class/net/%s/idletimer",
                               iap->interface_name);
  io = g_io_channel_new_file(sysfs_path, "r", NULL);

  g_free(sysfs_path);

  if (!io)
  {
    ILOG_ERR("idle timer failed to create io channel for iap %p", iap);
    return FALSE;
  }

  iap->idletimer_id = g_io_add_watch(io, G_IO_HUP | G_IO_ERR | G_IO_IN,
                                     icd_idle_timer_trigger, iap);
  g_io_channel_unref(io);

  ILOG_INFO("idle timer timeout set to %d minute(s)", timeout);

  return TRUE;
}

/**
 * Go through each IAP and see if the changed idle timer entry applies to the
 * type of this IAP
 *
 * @param  iap        the IAP
 * @param  user_data  the changed GConfEntry
 *
 * @return TRUE to go through all IAPs
 */
static gboolean
icd_idle_timer_foreach_iap(struct icd_iap *iap, gpointer user_data)
{
  const GConfEntry *entry;
  gchar *key;
  gchar *network_type;

  entry = (const GConfEntry *)user_data;

  if (!entry)
    return FALSE;

  key = g_strdup(gconf_entry_get_key(entry));
  network_type = g_strrstr(key, "/");

  if (!network_type)
    goto out;

  *network_type = 0;
  network_type = g_strrstr(key, "/");

  if (!network_type)
    goto out;

  network_type++;

  if (iap->connection.network_type &&
      !strcmp(network_type, iap->connection.network_type))
  {
    GConfValue *val = gconf_entry_get_value(entry);
    gint reset_timeout = 0;

    if (val)
    {
      reset_timeout = gconf_value_get_int(val);

      if (reset_timeout < 0)
      {
        ILOG_WARN("idle timer reset value %d cannot be negative",
                  reset_timeout);
        reset_timeout = 0;
      }
    }

    icd_idle_timer_unset(iap);
    icd_idle_timer_start(iap, reset_timeout);

    ILOG_DEBUG("idle timer update for iap %p, interface '%s', network type '%s', value %d sec",
               iap, iap->interface_name, network_type, reset_timeout);

    g_free(key);
    return TRUE;
  }

out:
  g_free(key);

  return FALSE;
}

/**
 * Callback for GConf entry change
 *
 * @param client     GConf client
 * @param cnxn_id    connection id
 * @param entry      the changed GConfEntry
 * @param user_data  user data
 */
static void
icd_idle_timer_gconf_changed(GConfClient *client, guint cnxn_id,
                             GConfEntry *entry, gpointer user_data)
{
  if (g_str_has_suffix(gconf_entry_get_key(entry), "idle_timeout"))
    icd_iap_foreach(icd_idle_timer_foreach_iap, entry);
}

/**
 * Clear all OUTPUT chain rules and add notification function for idle timer
 * gconf values
 *
 * @param  icd_ctx  icd context
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_idle_timer_init (struct icd_context *icd_ctx)
{
  GConfClient * gconf = gconf_client_get_default();

  gconf_client_add_dir(gconf, ICD_GCONF_NETWORK_MAPPING,
                       GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

  icd_ctx->idle_timer_notify =
      gconf_client_notify_add(gconf, ICD_GCONF_NETWORK_MAPPING,
                              icd_idle_timer_gconf_changed, icd_ctx,
                              NULL, NULL);
  g_object_unref(gconf);

  return !!icd_ctx->idle_timer_notify;
}

/**
 * Set idle timer for IAP
 * @param  iap  the IAP
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_idle_timer_set(struct icd_iap *iap)
{
  gchar *key;
  GConfClient *gconf;
  gint timeout;
  GError *err = NULL;

  if (iap->idletimer_id)
  {
    ILOG_CRIT("idle timer already set for iap %p", iap);
    return FALSE;
  }

  if (!iap->interface_name || !iap->connection.network_type)
  {
    ILOG_WARN("idle timer cannot be set for interface '%s', network type '%s'",
              iap->interface_name, iap->connection.network_type);
    return FALSE;
  }

  key = ICD_GCONF_NETWORK_IDLE_TIMEOUT(iap->connection.network_type);

  ILOG_DEBUG(
        "idle timer type '%s', key '%s'", iap->connection.network_type, key);

  if (!key)
  {
    ILOG_ERR("Unable to allocate idle timer gconf key");
    return FALSE;
  }

  gconf = gconf_client_get_default();
  timeout = gconf_client_get_int(gconf, key, &err);
  g_object_unref(gconf);
  g_free(key);

  if (err)
  {
    ILOG_WARN("idle timer value not found for network type '%s'",
              iap->connection.network_type);
    g_error_free(err);
    return FALSE;
  }

  if (timeout < 0)
  {
    ILOG_WARN("idle timer value %d cannot be negative", timeout);
    timeout = 0;
  }

  return icd_idle_timer_start(iap, timeout);
}

/**
 * Remove gconf idle timer notification
 * @param icd_ctx  icd context
 */
void
icd_idle_timer_remove(struct icd_context *icd_ctx)
{
  GConfClient *gconf = gconf_client_get_default();

  if (icd_ctx->idle_timer_notify)
  {
    gconf_client_notify_remove(gconf, icd_ctx->idle_timer_notify);
    gconf_client_remove_dir(gconf, ICD_GCONF_NETWORK_MAPPING, NULL);
  }

  g_object_unref(gconf);
}
