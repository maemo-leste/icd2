#ifndef ICD_EXEC_H
#define ICD_EXEC_H

/**
@file icd_exec.h

@author patrik.flykt@nokia.com

@copyright (C) 2007 Nokia Corporation. All rights reserved.

@addtogroup icd_exec ICd execution

@ingroup internal

 * @{ */

/** exit status */
enum icd_exit_status {
        EXIT_FORK_FAILED = 2,
        EXIT_SETSID_FAILED,
        EXIT_FILE_OPEN_FAILED,
        EXIT_DUP_FAILED,
        EXIT_DBUS_ERROR,                 /* not used by icd */
        EXIT_REGISTER_ERROR,             /* not used by icd */
        EXIT_GCONF_ERROR,                /* not used by icd */
        EXIT_SOCKETPAIR_FAILED,
        EXIT_SIGNAL_HANDLERS_FAILED,
        EXIT_WATCH_FAILED,
        EXIT_ANOTHER_INSTANCE_RUNNING,
        EXIT_FAILED_TO_INIT,
        EXIT_PID_WRITE_FAILED            /* new in icd2 */
};

/** @} */

#endif
