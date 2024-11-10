include mk/common.mk

CFLAGS += -O2 -std=c99
CFLAGS += -Wall -Wextra

BIN := rb-tests rb-perf

rb-tests: rbtree.o rb-tests.o
	$(CC) -o $@ $^

rb-perf: rbtree.o rb-perf.o
	$(CC) -o $@ $^

.DEFAULT_GOAL := all
all: $(BIN)

check: $(BIN)
	./rb-tests
	./rb-perf

.PHONY: clean
clean:
	$(RM) $(BIN) *.o
