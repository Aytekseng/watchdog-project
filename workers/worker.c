#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// print usage to stderr
static void usage(const char *prog){
    fprintf(stderr, "usage: %s loop | exit <sec> | crash <sec> | stubborn\n",prog);
}

// ignore the given signal using sigaction
static int ignore_signal(int sig){
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    return sigaction(sig, &sa, NULL);
}

// parse a non-negative number of seconds, -1 on bad input
static int parse_seconds(const char *s, int *out){
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < 0 || v > 3600)
        return -1;
    *out = (int)v;
    return 0;
}

int main(int argc, char **argv){
    int sec;

    if (argc < 2){
        usage(argv[0]);
        return 2;
    }

    // Ctrl+C is sent to the whole group, let the watchdog handle shutdown
    if (ignore_signal(SIGINT) == -1){
        perror("sigaction");
        return 2;
    }

    fprintf(stderr, "[worker %d] mode=%s\n", (int)getpid(), argv[1]);

    // run until killed
    if(strcmp(argv[1], "loop") == 0){
        for(;;)
            pause();
    }

    // ignore SIGTERM, only SIGKILL can stop it
    if(strcmp(argv[1], "stubborn") == 0){
        if (ignore_signal(SIGTERM) == -1){
            perror("sigaction");
            return 2;
        }
        for(;;)
            pause();
    }

    // wait, then exit with 0 (exit) or 1 (crash)
    if(strcmp(argv[1], "exit") == 0 || strcmp(argv[1], "crash") == 0){
        if(argc != 3 || parse_seconds(argv[2], &sec) == -1){
            usage(argv[0]);
            return 2;
        }
        sleep(sec);
        return strcmp(argv[1], "exit") == 0 ? 0 : 1;
    }

    usage(argv[0]);
    return 2;
}