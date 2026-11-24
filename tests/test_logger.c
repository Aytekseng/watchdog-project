#include "logger.h"

#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Stand-in for the process manager when testing only the logger. */
const char *pm_state_name(child_state_t state)
{
    assert(state == CH_RUNNING);
    return "RUNNING";
}

int main(void)
{
    char path[] = "/tmp/watchdog-logger-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(close(fd) == 0);

    child_t child = {"worker-1", 1234, CH_RUNNING, 1};
    assert(logger_open(path) == 0);
    assert(logger_event("START", "name=%s pid=%ld", child.name,
                        (long)child.pid) == 0);
    assert(logger_close() == 0);

    assert(logger_open(path) == 0);
    assert(logger_status(&child, 1) == 0);

    /* Read before closing to make sure records are flushed immediately. */
    FILE *input = fopen(path, "r");
    assert(input != NULL);
    assert(unlink(path) == 0);

    const char *expected[] = {
        "START name=worker-1 pid=1234\n",
        "STATUS_BEGIN children=1\n",
        "STATUS name=worker-1 pid=1234 state=RUNNING restarts=1\n",
        "STATUS_END\n"
    };
    char line[256];
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        assert(fgets(line, sizeof(line), input) != NULL);
        assert(strlen(line) > 22 && line[0] == '[' && line[20] == ']');
        assert(strspn(line + 1, "0123456789- :") == 19);
        assert(line[5] == '-' && line[8] == '-' && line[11] == ' ');
        assert(line[14] == ':' && line[17] == ':');
        assert(strcmp(line + 22, expected[i]) == 0);
    }
    assert(fgets(line, sizeof(line), input) == NULL && feof(input));
    assert(fclose(input) == 0);
    assert(logger_close() == 0);
    puts("Logger: timestamp, append, flush and status report OK");
    return 0;
}
