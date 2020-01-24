/**
@file icd_pid.c
@copyright GNU GPLv2 or later

@addtogroup icd_pid Pid handling
@ingroup internal

 * @{ */

#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <glib.h>

#include "icd_pid.h"

/**
 * Write the process id to the pid file
 * @param pidfile  the full pathname of the pid file
 * @return  TRUE on success, FALSE on failure
 */
gboolean
icd_pid_write(const char *pidfile)
{
  FILE *fp;
  pid_t pid;

  fp = fopen(pidfile, "w");

  if (!fp)
    return FALSE;

  pid = getpid();
  fprintf(fp, "%d\n", pid);
  fclose(fp);

  return TRUE;
}

/**
 * Remove process id file
 * @param pidfile  the full pathname of the pid file
 * @return  TRUE on success, FALSE on failure
 */
gboolean
icd_pid_remove(const char *pidfile)
{
  return unlink(pidfile) != -1;
}

/**
 * Read the process id from the pid file
 * @param pidfile  the full pathname of the pid file
 * @return  the process id or -1 on error
 */
static
pid_t icd_pid_read(const char * pidfile)
{
  FILE *fp;
  int vars_read;
  struct stat stat_buf;
  int pid;

  if (stat(pidfile, &stat_buf) < 0)
    return -1;

  fp = fopen(pidfile, "r");

  if (!fp)
    return -1;

  vars_read = fscanf(fp, "%d", &pid);
  fclose(fp);

  if (vars_read != 1)
    return -1;

  return (pid_t)pid;
}

/**
 * Check whether the process id stored in the pid file exists. If pid file
 * exists, but is empty or contains invalid process id, pid file is removed
 *
 * @param pidfile  the full pathname of the pid file
 *
 * @return         0 if no pid file is found or no such process is running,
 *                 otherwise the process id of the existing process
 */
pid_t
icd_pid_check(const char *pidfile)
{
  pid_t pid = icd_pid_read(pidfile);

  if (pid <= 0 || (kill(pid, 0) < 0 && errno == ESRCH))
  {
    icd_pid_remove(pidfile);
    return 0;
  }

  return pid;
}

/** @} */
