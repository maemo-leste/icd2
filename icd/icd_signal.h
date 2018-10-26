#ifndef ICD_SIGNAL_H
#define ICD_SIGNAL_H

/**
 * @brief  The signal handler function
 *
 * @param  sig  the received signal
 */
typedef void (*icd_signal_handler_fn) (int sig);

int icd_signal_init (icd_signal_handler_fn signal_handler, ...);

#endif
