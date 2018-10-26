#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <glib.h>

#include "icd_pid.h"

/**
 * @brief  Write the process id to the pid file
 *
 * @param  pidfile  the full pathname of the pid file
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_pid_write(const char *pidfile)
{
  FILE *fp;
  long pid;

  fp = fopen(pidfile, "w");

  if (!fp)
    return FALSE;

  pid = getpid();
  fprintf(fp, "%ld\n", (long)pid);
  fclose(fp);

  return TRUE;
}

/**
 * @brief  Remove process id file
 *
 * @param  pidfile  the full pathname of the pid file
 *
 * @return TRUE on success, FALSE on failure
 */
gboolean
icd_pid_remove(const char *pidfile)
{
  return unlink(pidfile) != -1;
}

/**
 * @brief Read the process id from the pid file.
 *
 * @param pidfile the full pathname of the pid file
 *
 * @return the process id or -1 on error
 *
 */
static
glong icd_pid_read(const char * pidfile)
{
  FILE *fp;
  int vars_read;
  struct stat stat_buf;
  glong pid;

  if (stat(pidfile, &stat_buf) < 0)
    return -1;

  fp = fopen(pidfile, "r");

  if (!fp)
    return -1;

  vars_read = fscanf(fp, "%ld", &pid);
  fclose(fp);

  if (vars_read != 1)
    return -1;

  return pid;
}

/**
 * @brief Check wheter the process id stored in the pid file exists. If
 * pid file exists, but is empty or contains invalid process id,
 * pid file is removed
 *
 * @param pidfile the full pathname of the pid file
 *
 * @return 0 if no pid file is found or no such process is running,
 * otherwise the process id of the existing process
 *
 */
glong
icd_pid_check(const char *pidfile)
{
  glong pid = icd_pid_read(pidfile);

  if (pid <= 0 || (kill(pid, 0) < 0 && errno == ESRCH))
  {
    icd_pid_remove(pidfile);
    return 0;
  }

  return pid;
}
