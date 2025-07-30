include mk/common.mk

CFLAGS += -O2 -g -std=gnu99
CFLAGS += -Wall -Wextra
CFLAGS += -I.

# Enable sanitizers
# CFLAGS += -fsanitize=address
# LDFLAGS += -fsanitize=address

# Directories
TESTS_DIR := tests
SCRIPTS_DIR := scripts

BIN := rb-tests rb-perf rb-bench

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

.DEFAULT_GOAL := all
all: $(BIN)

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

.PHONY: clean bench report
clean:
	$(RM) $(BIN) *.o $(TESTS_DIR)/*.o bench-*.xml bench-*.png bench-*.txt
