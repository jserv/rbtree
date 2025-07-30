include mk/common.mk

CFLAGS += -O2 -g -std=gnu99
CFLAGS += -Wall -Wextra
CFLAGS += -I.

# Directories
TESTS_DIR := tests
SCRIPTS_DIR := scripts

BIN := rb-tests rb-perf rb-bench
ASAN_BIN := rb-tests-asan rb-perf-asan rb-bench-asan

# Build rules with tests directory
rb-tests: rbtree.o $(TESTS_DIR)/rb-tests.o
	$(CC) -o $@ $^ $(LDFLAGS)

rb-perf: rbtree.o $(TESTS_DIR)/rb-perf.o
	$(CC) -o $@ $^ $(LDFLAGS)

rb-bench: rbtree.o $(TESTS_DIR)/rb-bench.o
	$(CC) -o $@ $^ $(LDFLAGS)

# Pattern rule for compiling test sources
$(TESTS_DIR)/%.o: $(TESTS_DIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# AddressSanitizer specific rules
ASAN_CFLAGS := -O0 -g -std=gnu99 -Wall -Wextra -I. -fsanitize=address -fno-omit-frame-pointer
ASAN_LDFLAGS := -fsanitize=address

rbtree-asan.o: rbtree.c
	$(CC) $(ASAN_CFLAGS) -c -o $@ $<

$(TESTS_DIR)/%-asan.o: $(TESTS_DIR)/%.c
	$(CC) $(ASAN_CFLAGS) -c -o $@ $<

rb-tests-asan: rbtree-asan.o $(TESTS_DIR)/rb-tests-asan.o
	$(CC) -o $@ $^ $(ASAN_LDFLAGS)

rb-perf-asan: rbtree-asan.o $(TESTS_DIR)/rb-perf-asan.o
	$(CC) -o $@ $^ $(ASAN_LDFLAGS)

rb-bench-asan: rbtree-asan.o $(TESTS_DIR)/rb-bench-asan.o
	$(CC) -o $@ $^ $(ASAN_LDFLAGS)

.DEFAULT_GOAL := all
all: $(BIN)

.PHONY: help
help:
	@echo "Red-Black Tree Implementation - Available targets:"
	@echo ""
	@echo "Build targets:"
	@echo "  all           - Build all binaries (default)"
	@echo "  rb-tests      - Build unit tests"
	@echo "  rb-perf       - Build performance tests"
	@echo "  rb-bench      - Build benchmarking tool"
	@echo ""
	@echo "Test targets:"
	@echo "  check         - Run basic tests"
	@echo "  check-asan    - Run tests with AddressSanitizer"
	@echo "  check-valgrind- Run tests with Valgrind"
	@echo "  check-memory  - Run all memory validation tests"
	@echo ""
	@echo "Benchmark targets:"
	@echo "  bench         - Run benchmarks"
	@echo "  report        - Generate benchmark reports and plots"
	@echo ""
	@echo "Other targets:"
	@echo "  clean         - Remove all build artifacts"
	@echo "  help          - Show this help message"

check: $(BIN)
	./rb-tests
	./rb-perf 1000000

# Benchmark targets
bench: rb-bench
	./rb-bench

report: rb-bench $(SCRIPTS_DIR)/bench-plot.py $(SCRIPTS_DIR)/bench-analysis.py
	@echo "Generating benchmark data..."
	@./rb-bench --xml > bench-results.xml
	@echo "Creating visualization plots..."
	@python3 $(SCRIPTS_DIR)/bench-plot.py bench-results.xml --comparison -o bench-comparison
	@python3 $(SCRIPTS_DIR)/bench-analysis.py bench-results.xml --scalability bench-scalability.png
	@echo "Generating reports..."
	@python3 $(SCRIPTS_DIR)/bench-plot.py bench-results.xml --report bench-report.txt
	@python3 $(SCRIPTS_DIR)/bench-analysis.py bench-results.xml --report bench-detailed-report.txt
	@echo ""
	@echo "Benchmark analysis complete:"
	@echo "  Plots: bench-comparison.png, bench-scalability.png"
	@echo "  Reports: bench-report.txt, bench-detailed-report.txt"

# Memory validation targets
#
# check-asan: Build and run tests with AddressSanitizer
# check-valgrind: Run tests under Valgrind memcheck
# check-memory: Run both AddressSanitizer and Valgrind tests

.PHONY: check-asan
check-asan: $(ASAN_BIN)
	@echo "=== Running AddressSanitizer tests ==="
	./rb-tests-asan
	./rb-perf-asan 50
	./rb-perf-asan 10000
	@echo "=== AddressSanitizer tests completed ==="

.PHONY: check-valgrind
check-valgrind: $(BIN)
	@echo "=== Running Valgrind memory checks ==="
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./rb-tests
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./rb-perf 50
	valgrind --leak-check=full --show-leak-kinds=all --error-exitcode=1 ./rb-perf 10000
	@echo "=== Valgrind tests completed ==="

.PHONY: check-memory
check-memory: check-asan check-valgrind
	@echo "=== All memory validation tests passed ==="

.PHONY: clean bench report
clean:
	$(RM) $(BIN) $(ASAN_BIN) *.o $(TESTS_DIR)/*.o bench-*.xml bench-*.png bench-*.txt
