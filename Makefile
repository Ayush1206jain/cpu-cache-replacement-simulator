# CPU Cache Replacement Simulator -- Makefile
#
# Usage:
#   make           -> build simulator (all days)
#   make test4     -> Day 4 LRU unit tests
#   make test5     -> Day 5 FIFO + LFU + Unified tests
#   make test6     -> Day 6 Set-Associative cache tests
#   make test7     -> Day 7 Trace Reader + Simulator tests
#   make test      -> run all tests
#   make run       -> build + run simulator
#   make clean     -> remove build artifacts

CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
IFLAGS = -Isrc -Isrc/cache

# Source files
SRC_CACHE = src/cache/lru.c src/cache/fifo.c src/cache/lfu.c \
            src/cache/cache.c src/cache/set_cache.c
SRC_SIM   = src/trace.c src/simulator.c
SRC_MAIN  = src/main.c

.PHONY: all test test4 test5 test6 test7 run clean

all: simulator

simulator: $(SRC_MAIN) $(SRC_CACHE) $(SRC_SIM)
	$(CC) $(CFLAGS) $(IFLAGS) -o simulator $^
	@echo "Built: simulator"

test4: tests/test_lru.c src/cache/lru.c
	$(CC) $(CFLAGS) $(IFLAGS) -o test_lru $^
	@echo "Running Day 4 LRU tests..."
	.\test_lru.exe

test5: tests/test_day5.c $(SRC_CACHE)
	$(CC) $(CFLAGS) $(IFLAGS) -o test_day5 $^
	@echo "Running Day 5 FIFO+LFU+Unified tests..."
	.\test_day5.exe

test6: tests/test_day6.c $(SRC_CACHE)
	$(CC) $(CFLAGS) $(IFLAGS) -o test_day6 $^
	@echo "Running Day 6 Set-Associative tests..."
	.\test_day6.exe

test7: tests/test_day7.c $(SRC_CACHE) $(SRC_SIM)
	$(CC) $(CFLAGS) $(IFLAGS) -o test_day7 $^
	@echo "Running Day 7 Trace+Simulator tests..."
	.\test_day7.exe

test: test4 test5 test6 test7
	@echo "All tests complete."

run: simulator
	.\simulator.exe

clean:
	del /Q simulator.exe test_lru.exe test_day5.exe test_day6.exe test_day7.exe 2>nul || true
	@echo "Cleaned."
