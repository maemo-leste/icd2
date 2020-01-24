#ifndef ICD_PID_H
#define ICD_PID_H

#include <sys/types.h>
#include <glib.h>

gboolean icd_pid_remove (const char *pidfile);
pid_t    icd_pid_check  (const char *pidfile);
gboolean icd_pid_write  (const char *pidfile);

#endif
