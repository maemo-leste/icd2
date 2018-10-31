/**
@file icd_log.c

@author patrik.flykt@nokia.com

@copyright (C) 2007 Nokia Corporation. All rights reserved.

@addtogroup icd_log Logging functions

@ingroup support_libraries

 * @{ */

#include "icd_log.h"

/** the global log level */
static enum icd_loglevel loglevel = ICD_INFO;

/** enable logging */
void
icd_log_open(void)
{
  DLOG_OPEN(PACKAGE " " VERSION);
}

/** disable logging */
void
icd_log_close(void)
{
  closelog();
}

/** get the current log level */
enum icd_loglevel
icd_log_get_level(void)
{
  return loglevel;
}

/** set the current log level */
enum icd_loglevel
    icd_log_set_level(enum icd_loglevel new_level)
{
  enum icd_loglevel old_level;

  if (new_level >= ICD_CRIT)
    new_level = ICD_CRIT;

  old_level = loglevel;
  loglevel = new_level;

  return old_level;
}

/** toggle logging to the next level */
void
icd_log_nextlevel(void)
{
  if (loglevel)
    loglevel--;
  else
    loglevel = ICD_CRIT;

  syslog(30, "Log level set to %d", loglevel);
}

/** @} */
