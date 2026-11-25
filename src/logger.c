#include "logger.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static FILE *log_file;

int logger_open(const char *path)
{
    if (log_file != NULL) {
        errno = EBUSY;
        return -1;
    }

    /* Keep old records and close the descriptor when a worker runs exec(). */
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0666);
    if (fd == -1) {
        return -1;
    }

    log_file = fdopen(fd, "a");
    if (log_file == NULL) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }

    return 0;
}

int logger_event(const char *event, const char *format, ...)
{
    char timestamp[20];
    struct tm local;
    time_t now = time(NULL);

    if (log_file == NULL) {
        errno = EBADF;
        return -1;
    }
    if (now == (time_t)-1 || localtime_r(&now, &local) == NULL) {
        return -1;
    }
    if (strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
                 &local) == 0) {
        errno = EOVERFLOW;
        return -1;
    }

    if (fprintf(log_file, "[%s] %s", timestamp, event) < 0) {
        return -1;
    }
    if (format != NULL && *format != '\0') {
        if (fputc(' ', log_file) == EOF) {
            return -1;
        }

        va_list args;
        va_start(args, format);
        int result = vfprintf(log_file, format, args);
        va_end(args);
        if (result < 0) {
            return -1;
        }
    }
    if (fputc('\n', log_file) == EOF) {
        return -1;
    }
    /* Make new lines visible to tail while the watchdog is still running. */
    return fflush(log_file);
}

int logger_status(const child_t *children, int count)
{
    if (logger_event("STATUS_BEGIN", "children=%d", count) == -1) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        const child_t *child = &children[i];
        if (logger_event("STATUS", "name=%s pid=%ld state=%s restarts=%d",
                         child->name, (long)child->pid,
                         pm_state_name(child->state), child->restart_count) == -1) {
            return -1;
        }
    }

    return logger_event("STATUS_END", NULL);
}

int logger_close(void)
{
    if (log_file == NULL) {
        return 0;
    }

    int result = fclose(log_file);
    log_file = NULL;
    return result;
}
