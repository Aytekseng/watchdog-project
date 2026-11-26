#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "process_manager.h"
#include "logger.h"

#define WORKER_PATH "./workers/worker"
#define CHECK_INTERVAL_SEC 5 // periodic check period

// argv for each worker, NULL terminated for execv
static char *const worker1_argv[] = { WORKER_PATH, "loop", NULL };
static char *const worker2_argv[] = { WORKER_PATH, "exit", "5", NULL};
static char *const worker3_argv[] = { WORKER_PATH, "crash", "3", NULL};
static char *const *const worker_argv[] = { worker1_argv, worker2_argv, worker3_argv };

// child table, only this file can change it
static child_t children[MAX_CHILDREN] = {
    { "worker-1" , 0, CH_STOPPED, 0},
    { "worker-2" , 0, CH_STOPPED, 0},
    { "worker-3" , 0, CH_STOPPED, 0}
};
static const int child_count = 3;

// set by signal handlers, taken by the main loop
static volatile sig_atomic_t got_chld = 0;
static volatile sig_atomic_t got_alrm = 0;

// return 1 if SIGCHLD arrived and clear the flag
int pm_take_chld(void){
    int got = got_chld;
    got_chld = 0;
    return got;
}

// return 1 if SIGALRM arrived and clear the flag
int pm_take_alrm(void){
    int got = got_alrm;
    got_alrm = 0;
    return got;
}

// read-only view of the child table
const child_t *pm_get_children(int *count){
    *count = child_count;
    return children;
}

const char *pm_state_name(child_state_t s){
    switch (s)
    {
    case CH_RUNNING : return "RUNNING";
    case CH_EXITED : return "EXITED";
    case CH_FAILED : return "FAILED";
    case CH_STOPPED : return "STOPPED"; 
    }
    return "UNKNOWN";
}

// handlers only set a flag, the real work is done in the main loop
static void on_chld(int sig){
    (void)sig;
    got_chld = 1;
}

static void on_alrm(int sig){
    (void)sig;
    got_alrm = 1;
}

// install a handler with sigaction
static int install_handler(int sig, void (*handler)(int), int flags){
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    return sigaction(sig, &sa, NULL);
}

// install SIGCHLD/SIGALRM handlers and start the periodic timer
int pm_init(void){
    struct itimerval timer;

    if(install_handler(SIGCHLD, on_chld, SA_RESTART | SA_NOCLDSTOP) == -1)
        return -1;
    if(install_handler(SIGALRM, on_alrm, SA_RESTART) == -1)
        return -1;

    memset(&timer, 0, sizeof(timer));
    timer.it_value.tv_sec = CHECK_INTERVAL_SEC; // first alarm
    timer.it_interval.tv_sec = CHECK_INTERVAL_SEC; // then repeat
    return setitimer(ITIMER_REAL, &timer, NULL);
}

// fork and exec worker i, record its pid
// a close-on-exec pipe tells the parent whether exec really succeeded
static int spawn(int i){
    int fds[2];
    int child_errno = 0;
    ssize_t n;
    pid_t pid;

    if(pipe(fds) == -1){
        logger_event("FORK_FAILED", "name=%s errno=%d", children[i].name, errno);
        return -1;
    }
    // write end closes itself on a successful exec
    if(fcntl(fds[1], F_SETFD, FD_CLOEXEC) == -1){
        int saved = errno;
        close(fds[0]);
        close(fds[1]);
        logger_event("FORK_FAILED", "name=%s errno=%d", children[i].name, saved);
        errno = saved;
        return -1;
    }

    pid = fork();
    if(pid == -1){
        int saved = errno;
        close(fds[0]);
        close(fds[1]);
        logger_event("FORK_FAILED", "name=%s errno=%d", children[i].name, saved);
        errno = saved;
        return -1;
    }

    if(pid == 0){
        // child: start with an empty signal mask, the parent may have blocked some
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, NULL);
        close(fds[0]);

        execv(WORKER_PATH, worker_argv[i]);

        // only reached if exec failed: tell the parent why
        child_errno = errno;
        (void)!write(fds[1], &child_errno, sizeof(child_errno));
        _exit(127);
    }

    // parent: 0 bytes means exec succeeded, otherwise the child sent errno
    close(fds[1]);
    do {
        n = read(fds[0], &child_errno, sizeof(child_errno));
    } while(n == -1 && errno == EINTR);
    close(fds[0]);

    if(n > 0){
        pid_t r;
        do {
            r = waitpid(pid, NULL, 0);   // reap the failed child, no zombie
        } while(r == -1 && errno == EINTR);
        logger_event("EXEC_FAILED", "name=%s errno=%d", children[i].name, child_errno);
        errno = child_errno;
        return -1;
    }

    children[i].pid = pid;
    children[i].state = CH_RUNNING;
    return 0;
}

// start every worker once
int pm_start_all(void){
    int result = 0;
    
    // fail early if the worker program cannot be executed
    if (access(WORKER_PATH, X_OK) == -1){
        int saved = errno;
        logger_event("START_FAILED", "path=%s errno=%d", WORKER_PATH, saved);
        errno = saved;   // keep errno for perror() in main
        return -1;
    }

    for(int i = 0; i < child_count; i++){
        if(spawn(i) == -1){
            result = -1;
            continue;
        }
        logger_event("START", "name=%s pid=%ld mode=%s", children[i].name, (long)children[i].pid, worker_argv[i][1]);
    }

    return result;
}

// index of the child with this pid, -1 if not ours
static int find_child(pid_t pid){
    for(int i = 0; i < child_count; i++){
        if(children[i].pid == pid)
            return i;
    }
    return -1;
}

// decide what to do with a child that has terminated
static void handle_exit(int i, int status, int shutting_down){
    child_t *c = &children[i];
    long old_pid = (long)c->pid;

    c->pid = 0;

    // normal exit: never restarted
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0){
        c->state = CH_EXITED;
        logger_event("EXIT", "name=%s pid=%ld code=0", c->name, old_pid);
        return;
    }

    // watchdog is shutting down: do not restart
    if (shutting_down){
        c->state = CH_STOPPED;
        logger_event("STOPPED", "name=%s pid=%ld", c->name, old_pid);
        return;
    }

    // unexpected termination: error code or signal
    if (WIFEXITED(status))
        logger_event("CRASH", "name=%s pid=%ld code=%d", c->name, old_pid, WEXITSTATUS(status));
    else if (WIFSIGNALED(status))
        logger_event("CRASH", "name=%s pid=%ld signal=%d", c->name, old_pid, WTERMSIG(status));

    if (c->restart_count >= MAX_RESTARTS){
        c->state = CH_FAILED;
        logger_event("RESTART_LIMIT", "name=%s restarts=%d", c->name, c->restart_count);
        return;
    }

    c->restart_count++;
    if (spawn(i) == -1){
        c->state = CH_FAILED;
        return;
    }
    logger_event("RESTART", "name=%s pid=%ld attempt=%d", c->name, (long)c->pid, c->restart_count);
}

// reap every terminated child, restart the ones that crashed
void pm_reap_children(int shutting_down){
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0){
        int i = find_child(pid);
        if (i != -1)
            handle_exit(i, status, shutting_down);
    }
}

// number of children still running
static int running_count(void){
    int n = 0;

    for (int i = 0; i < child_count; i++){
        if (children[i].state == CH_RUNNING)
            n++;
    }
    return n;
}

// called on SIGALRM: reap as a safety net and log a heartbeat
void pm_periodic_check(void){
    int failed = 0;

    pm_reap_children(0);

    for (int i = 0; i < child_count; i++){
        if (children[i].state == CH_FAILED)
            failed++;
    }
    logger_event("HEARTBEAT", "running=%d failed=%d", running_count(), failed);
}

// SIGTERM all running children, SIGKILL the ones still alive after timeout_sec
void pm_terminate_all(int timeout_sec){
    time_t deadline;
    int status;

    pm_reap_children(1);   // collect anything that already died

    for (int i = 0; i < child_count; i++){
        if (children[i].state != CH_RUNNING)
            continue;
        if (kill(children[i].pid, SIGTERM) == 0)
            logger_event("TERMINATE", "name=%s pid=%ld", children[i].name, (long)children[i].pid);
    }

    deadline = time(NULL) + timeout_sec;
    while (running_count() > 0 && time(NULL) < deadline){
        struct timespec nap = { 0, 100000000L };   // 100 ms
        nanosleep(&nap, NULL);   // a signal may wake us early, the deadline still holds
        pm_reap_children(1);
    }

    // still alive after the timeout: force kill and wait for each one
    for (int i = 0; i < child_count; i++){
        pid_t r;

        if (children[i].state != CH_RUNNING)
            continue;
        if (kill(children[i].pid, SIGKILL) == 0)
            logger_event("KILL", "name=%s pid=%ld", children[i].name, (long)children[i].pid);

        do {
            r = waitpid(children[i].pid, &status, 0);
        } while (r == -1 && errno == EINTR);

        if (r == children[i].pid){
            handle_exit(i, status, 1);
        } else {
            children[i].pid = 0;
            children[i].state = CH_STOPPED;
        }
    }
}
