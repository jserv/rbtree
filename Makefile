include mk/common.mk

CFLAGS += -O2 -g -std=gnu99
CFLAGS += -Wall -Wextra

# Enable sanitizers
# CFLAGS += -fsanitize=address
# LDFLAGS += -fsanitize=address

BIN := rb-tests rb-perf

rb-tests: rbtree.o rb-tests.o
	$(CC) -o $@ $^ $(LDFLAGS)

rb-perf: rbtree.o rb-perf.o
	$(CC) -o $@ $^ $(LDFLAGS)

.DEFAULT_GOAL := all
all: $(BIN)

check: $(BIN)
	./rb-tests
	./rb-perf 50

.PHONY: clean
clean:
	$(RM) $(BIN) *.o
