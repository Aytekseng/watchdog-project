#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <sys/types.h>

#define MAX_RESTARTS 3 // max restarts per child
#define MAX_CHILDREN 8 // child table capacity

typedef enum {
    CH_RUNNING, // alive
    CH_EXITED,  // exited with code 0, not restarted
    CH_FAILED,  // restart limit reached
    CH_STOPPED  // stopped by watchdog on shutdown
} child_state_t;

typedef struct {
    const char *name;     // worker name, e.g. "worker-1"
    pid_t pid;            // current pid, 0 if not running
    child_state_t state;  // current state
    int restart_count;    // restarts so far
} child_t;

int pm_init(void);      // install SIGCHLD/SIGALRM handlers and timer, -1 on error
int pm_start_all(void); // fork and exec all workers, -1 on error
int pm_take_chld(void); // 1 if SIGCHLD arrived, clears the flag
int pm_take_alrm(void); // 1 if SIGALRM arrived, clears the flag

void pm_reap_children(int shutting_down); // reap dead children, restart if allowed
void pm_periodic_check(void);             // periodic health check on SIGALRM
void pm_terminate_all(int timeout_sec);   // SIGTERM all, SIGKILL after timeout

const child_t *pm_get_children(int *count); // read-only child table, size in *count
const char *pm_state_name(child_state_t s); // state as a string

#endif
