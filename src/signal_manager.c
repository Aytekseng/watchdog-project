#define _POSIX_C_SOURCE 200809L

#include<signal.h>
#include<stddef.h>

#include "signal_manager.h"

static volatile sig_atomic_t shutdown_requested=0;
static volatile sig_atomic_t status_requested=0;

/* Record requests for the main loop. */
static void handle_signal(int signum)
{
    if(signum==SIGUSR1)
    {
        status_requested =1;
    } else if(signum == SIGINT || signum == SIGTERM)
    {
        shutdown_requested=1;
    }
}

int sm_init(void){
    struct sigaction action = {0};

    action.sa_handler = handle_signal;
    action.sa_flags= SA_RESTART;

    /* Block no additional signals while the handler runs. */
    if(sigemptyset(&action.sa_mask) == -1){
        return -1;
    }

     /* Register the same handler for all three signals. */
    if(sigaction(SIGINT, &action, NULL) ==-1 ||
       sigaction(SIGTERM, &action, NULL) == -1 ||
       sigaction(SIGUSR1, &action, NULL) == -1){
       return -1;
       }

       return 0;
}

int sm_shutdown_requested(void){
    return shutdown_requested;
}

int sm_take_status_request(void)
{
    /* Save the pending request before clearing the flag. */
    int requested = status_requested;
    status_requested = 0;
    return requested;
}