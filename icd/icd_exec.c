#define _GNU_SOURCE

#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <glib-object.h>
#include "config.h"
#include "icd_log.h"
#include "icd_pid.h"
#include "icd_args.h"
#include "icd_signal.h"
#include "icd_context.h"
#include "icd_exec.h"
#include "icd_scan.h"
#include "icd_network_api.h"
#include "icd_request.h"
#include "icd_policy_api.h"
#include "icd_osso_ic.h"
#include "icd_name_owner.h"
#include "icd_idle_timer.h"
#include "icd_gconf.h"
#include "icd_script.h"
#include "icd_dbus_api.h"
#include "icd_srv_provider.h"
#include "icd_network_priority.h"


/** the pid file identical to previous version of icd */
#define PIDFILE "/var/run/icd2.pid"
/** shutdown timeout function interval in ms */
#define ICD_SHUTDOWN_TIMEOUT 100

/**
 * Wait until the last request has exited and quit ICd
 * @param  icd_ctx  icd context
 * @return TRUE while waiting, FALSE when all requests have been removed
 */
static gboolean
icd_exec_shutdown_check(struct icd_context *icd_ctx)
{
  if (icd_ctx->request_list)
  {
    static gint suppress_log = 0;

    if (!suppress_log)
      ILOG_INFO("requests pending, waiting with shutdown");

    suppress_log = (suppress_log + 1) % 20u;
    return TRUE;
  }

  ILOG_DEBUG("icd context shutting down");
  icd_context_stop();

  return FALSE;
}

/**
 * Cancel all requests and shut down ICd
 * @param icd_ctx  icd context
 */
static void
icd_exec_shutdown (struct icd_context *icd_ctx)
{
  GSList *req_list;

  if (icd_ctx->shutting_down)
  {
    ILOG_DEBUG("icd already shutting down");
    return;
  }

  icd_dbus_api_deinit();
  icd_osso_ic_deinit();
  ILOG_INFO("Cancelling all requests");

  for (req_list = icd_ctx->request_list; req_list; req_list = req_list->next)
  {
      struct icd_request *req = (struct icd_request *)req_list->data;

      if (!req)
      {
        ILOG_ERR("Request in list is NULL");
        continue;
      }

      ILOG_DEBUG("Cancelling request %p", req);
      icd_request_cancel(req, ICD_POLICY_ATTRIBUTE_CONN_UI);
  }

  icd_ctx->shutting_down = g_timeout_add(
        ICD_SHUTDOWN_TIMEOUT, (GSourceFunc)icd_exec_shutdown_check, icd_ctx);
}

/**
 * Callback to handle posix signals
 * @param sig  signal received
 */
static void
icd_exec_signal_cb(int sig)
{
  ILOG_DEBUG("signal received: %s", strsignal(sig));

  switch (sig)
  {
    case SIGINT:
    case SIGTERM:
    {
      icd_exec_shutdown(icd_context_get());
      break;
    }
    case SIGUSR1:
      icd_log_nextlevel();
      break;
    case SIGCHLD:
      while (1)
      {
        struct icd_context *icd_ctx;
        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);

        if (pid <= 0)
          break;

        ILOG_INFO("pid %d has exited with status %d", pid, status);
        icd_ctx = icd_context_get();

        if (!icd_script_notify_pid(pid, status) &&
            !icd_network_api_notify_pid(icd_ctx, pid, status) &&
            !icd_srv_provider_notify_pid(icd_ctx, pid, status))
        {
          ILOG_WARN("no module or script found to use pid %d", pid);
        }
      }
      break;
    default:
      ILOG_DEBUG("Signal received: %s; continuing", strsignal(sig));
      break;
  }
}

/**
 * Main function
 *
 * @param  argc  argc
 * @param  argv  argv
 *
 * @return 0 normal termination, >0 on error
 */
int
main(int argc, char **argv)
{
  struct icd_context *icd_ctx;
  gint pid;
  int rv = 0;

  icd_context_init();
  icd_log_open();

  ILOG_INFO(PACKAGE" version "ICD_NW_MODULE_VERSION" starting");

#if !GLIB_CHECK_VERSION (2,35,0)
  g_type_init();
#endif

  icd_ctx = icd_context_get();
  icd_args_decode(argc, argv, icd_ctx);

  pid = icd_pid_check(PIDFILE);

  if (pid)
  {
    ILOG_ERR("Unable to run: another instance running (PID %d)", pid);
    rv = EXIT_ANOTHER_INSTANCE_RUNNING;
  }
  else if (icd_ctx->daemon && (errno = 0, daemon(0, 0) == -1))
  {
    if (errno)
      ILOG_ERR("Unable to run as daemon");
    else
      ILOG_ERR("Unable to run as daemon, unusable /dev/null");

    rv = EXIT_FORK_FAILED;
  }
  else if (icd_pid_write(PIDFILE))
  {
    rv = icd_signal_init(icd_exec_signal_cb, SIGINT, SIGTERM, SIGUSR1, SIGUSR2,
                         SIGCHLD, SIGHUP, -1);
    if (!rv)
    {
      icd_idle_timer_init(icd_ctx);
      icd_network_api_load_modules(icd_ctx);
      icd_srv_provider_load_modules(icd_ctx);

      if (icd_policy_api_load_modules(icd_ctx) && icd_name_owner_init(icd_ctx) &&
          icd_osso_ic_init(icd_ctx) && icd_dbus_api_init())
      {
        icd_network_priority_pref_init();
        icd_gconf_remove_temporary(NULL);
        icd_gconf_add_notify();

        ILOG_INFO("Running context...");

        icd_context_run();
        icd_gconf_del_notify();
      }

      icd_policy_api_unload_modules(icd_ctx);
      icd_srv_provider_unload_modules(icd_ctx);
      icd_network_api_unload_modules(icd_ctx);
      icd_idle_timer_remove(icd_ctx);
      icd_context_destroy();
      icd_pid_remove(PIDFILE);
    }
  }
  else
  {
    ILOG_ERR("Unable to run: cannot write pid file "PIDFILE);
    rv = EXIT_PID_WRITE_FAILED;
  }

  ILOG_INFO(PACKAGE" version "ICD_NW_MODULE_VERSION" exited");

  icd_log_close();

  return rv;
}
