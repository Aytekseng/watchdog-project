#ifndef LOGGER_H
#define LOGGER_H

#include "process_manager.h"

/* Open in append mode. The watchdog calls these outside signal handlers. */
int logger_open(const char *path);
int logger_event(const char *event, const char *format, ...);
int logger_status(const child_t *children, int count);
int logger_close(void);

#endif
