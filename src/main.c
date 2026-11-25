#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "signal_manager.h"
#include "logger.h"


static int wait_for_events(const sigset_t *event_signals, int *status_requested)
{
    sigset_t previous_mask;

    /* Block signals while checking and clearing the request flags. */
    if (sigprocmask(SIG_BLOCK, event_signals, &previous_mask) == -1) {
        return -1;
    }

    *status_requested = 0;

    while (!sm_shutdown_requested()) {
        *status_requested = sm_take_status_request();

        if (*status_requested) {
            break;
        }

        /*
         * Temporarily use the previous mask and wait in one step.
         * EINTR is expected when a signal handler returns.
         */
        if (sigsuspend(&previous_mask) == -1 && errno != EINTR) {
            /* Preserve the wait error while restoring the mask. */
            int wait_error = errno;
            (void)sigprocmask(SIG_SETMASK, &previous_mask, NULL);
            errno = wait_error;
            return -1;
        }
    }

    /* Restore the caller's mask before it processes the event. */
    return sigprocmask(SIG_SETMASK, &previous_mask, NULL);
}

int main(void)
{
    sigset_t event_signals;
    sigset_t previous_mask;

    sigemptyset(&event_signals);
    sigaddset(&event_signals, SIGINT);
    sigaddset(&event_signals, SIGTERM);
    sigaddset(&event_signals, SIGUSR1);

    /* Defer delivery of event signals until their handlers are installed. */
    if (sigprocmask(SIG_BLOCK, &event_signals, &previous_mask) == -1) {
        perror("sigprocmask");
        return EXIT_FAILURE;
    }

    if (sm_init() == -1) {
        perror("Signal setup");
        return EXIT_FAILURE;
    }

    if(logger_open("watchdog.log") == -1){
        perror("watchdog.log");
        return EXIT_FAILURE;
    }

    /* Restore the startup mask before entering the event loop. */
    if (sigprocmask(SIG_SETMASK, &previous_mask, NULL) == -1) {
        perror("sigprocmask");
        (void)logger_close();
        return EXIT_FAILURE;
    }

    int exit_status = EXIT_SUCCESS;

    while (!sm_shutdown_requested()) {
        int status_requested;

        if (wait_for_events(&event_signals, &status_requested) == -1) {
            perror("Waiting for signals");
            exit_status = EXIT_FAILURE;
            break;
        }

        if (!sm_shutdown_requested() && status_requested) {
            /* Replace this message with the status report during integration. */
            puts("Status request received.");
        }
    }

    if(logger_close() == -1)
    {
        perror("Closing watchdog.log");
        exit_status = EXIT_FAILURE;
    }

    return exit_status;
}