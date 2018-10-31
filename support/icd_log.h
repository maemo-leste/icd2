#ifndef ICD_LOG_H
#define ICD_LOG_H

#include "config.h"

#ifdef ICD_LOG_STDERR
#define OSSOLOG_STDERR
#endif
#define OSSOLOG_COMPILE
#include <osso-log.h>

/** level to log on */
enum icd_loglevel {
  ICD_DEBUG = 0,
  ICD_INFO,
  ICD_WARN,
  ICD_ERR,
  ICD_CRIT
};

/** log on level DEBUG */
#define ILOG_DEBUG(...) do {   \
    if (icd_log_get_level() <= ICD_DEBUG) \
       DLOG_DEBUG(__VA_ARGS__); \
} while (0)

/** log on level INFO */
#define ILOG_INFO(...) do {   \
    if (icd_log_get_level() <= ICD_INFO) \
       DLOG_INFO(__VA_ARGS__); \
} while (0)

/** log on level WARN */
#define ILOG_WARN(...) do {   \
    if (icd_log_get_level() <= ICD_WARN) \
       DLOG_WARN(__VA_ARGS__); \
} while (0)

/** log on level ERR */
#define ILOG_ERR(...) do { \
    if (icd_log_get_level() <= ICD_ERR) \
       DLOG_ERR(__VA_ARGS__); \
} while (0)

/** log on level CRIT */
#define ILOG_CRIT(...) do {   \
    if (icd_log_get_level() <= ICD_CRIT) \
       DLOG_CRIT(__VA_ARGS__); \
} while (0)

void icd_log_open(void);

void icd_log_close(void);

enum icd_loglevel icd_log_get_level (void);

enum icd_loglevel icd_log_set_level (enum icd_loglevel new_level);

void icd_log_nextlevel (void);

/* @} */

#endif
