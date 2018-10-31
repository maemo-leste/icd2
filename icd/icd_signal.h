#ifndef ICD_SIGNAL_H
#define ICD_SIGNAL_H

/**
@file icd_signal.h

@author patrik.flykt@nokia.com

@copyright (C) 2007 Nokia Corporation. All rights reserved.

@addtogroup icd_signal Signal handling integration with glib main loop

@ingroup internal

 * @{ */

/** The signal handler function
 * @param sig  the received signal
 */
typedef void (*icd_signal_handler_fn) (int sig);

int icd_signal_init (icd_signal_handler_fn signal_handler, ...);

/** @} */

#endif
