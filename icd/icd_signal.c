#include <glib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <signal.h>
#include <errno.h>

#include "icd_signal.h"
#include "icd_exec.h"
#include "icd_log.h"

/** pipe used in communication between the posix signal handler and the glib
 * main loop */
static int signal_pipe [2];

/**
 * Signal handler.
 * @param sig  Received signal number.
 * @note  Only queues received signals on pipe which is polled by main
 *        thread. Thus signals are handler synchronously on specific point.
 */
static void
icd_signal_handler(int sig)
{
  send(signal_pipe[1], &sig, sizeof(sig), MSG_DONTWAIT);
}

/**
 * GLib callback called from main loop when the signal pipe receives data.
 * This function only polls the signal pipe and calls signal_handler to do
 * the actual work.
 *
 * @param  chan  IO Channel associated with file descriptor.
 * @param  cond  Condition which triggered callback.
 * @param  data  the signal handler function to call
 *
 * @return always TRUE
 */
static gboolean
icd_signal_cb(GIOChannel *chan, GIOCondition cond, gpointer data)
{
  icd_signal_handler_fn cb = (icd_signal_handler_fn)data;
  int fd;
  int sig;

  fd = g_io_channel_unix_get_fd(chan);

  if (fd >= 0)
  {
    while (recv(fd, &sig, sizeof(sig), MSG_DONTWAIT) == sizeof(sig))
    {
      if (cb)
        cb(sig);
    }
  }

  return TRUE;
}

/**
 * Initialize signal handling. Create pipe for queueing signals from signal
 * handler to main thread. Hook all processed signals and create a Glib io
 * watch for the pipe.
 *
 * @param  signal_handler  function handling the signal
 * @param  ...             signals to watch, end with -1
 *
 * @return 0 on success, #icd_exit_status on failure
 */
int
icd_signal_init(icd_signal_handler_fn signal_handler, ...)
{
  int status;
  GIOChannel *io;
  int sig;
  va_list ap;

  if (socketpair(AF_LOCAL, SOCK_STREAM, 0, signal_pipe) < 0)
  {
    ILOG_ERR("Failed to create socketpair for signal handling");
    return EXIT_SOCKETPAIR_FAILED;
  }

  va_start(ap, signal_handler);
  sig = va_arg(ap, int);

  while (sig != -1)
  {
    if (signal(sig, icd_signal_handler) == SIG_ERR)
    {
      ILOG_ERR("Failed to setup signal handler for signal %d[%d]", sig, errno);
      status = EXIT_SIGNAL_HANDLERS_FAILED;
      goto out;
    }

    sig = va_arg(ap, int);
  }

  io = g_io_channel_unix_new(signal_pipe[0]);
  if (io)
  {
    g_io_add_watch(io, G_IO_HUP|G_IO_ERR|G_IO_PRI|G_IO_IN, icd_signal_cb,
                   signal_handler);
    status = 0;
  }
  else
  {
    ILOG_ERR("Unable to allocate IO channel watch");
    status = EXIT_WATCH_FAILED;
  }

out:
  va_end(ap);
  return status;
}
