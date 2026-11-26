CC = gcc
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -Iinclude
CFLAGS ?= -std=c11 -Wall -Wextra -g

WATCHDOG_OBJS = src/main.o src/signal_manager.o src/process_manager.o src/logger.o
WORKER_OBJ = workers/worker.o
TEST_OBJ = tests/test_logger.o

.PHONY: all test test-logger clean

all: watchdog workers/worker

watchdog: $(WATCHDOG_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

workers/worker: $(WORKER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

tests/test_logger: $(TEST_OBJ) src/logger.o
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

src/main.o: include/signal_manager.h include/process_manager.h include/logger.h
src/signal_manager.o: include/signal_manager.h
src/process_manager.o: include/process_manager.h include/logger.h
src/logger.o tests/test_logger.o: include/logger.h include/process_manager.h

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

test-logger: tests/test_logger
	./tests/test_logger

test: all test-logger
	bash tests/test_scenarios.sh

clean:
	$(RM) watchdog workers/worker tests/test_logger $(WATCHDOG_OBJS) $(WORKER_OBJ) $(TEST_OBJ)
