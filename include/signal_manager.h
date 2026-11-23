#ifndef SIGNAL_MANAGER_H
#define SIGNAL_MANAGER_H

/* Install signal handlers. Return 0 on success or -1 on error. */
int sm_init(void);

/* Read the shutdown flag without clearing it. */
int sm_shutdown_requested(void);

/* Read and clear the status flag. The caller must block SIGUSR1. */
int sm_take_status_request(void);

#endif