# CPU Cache Replacement Simulator — Makefile
#
# Usage:
#   make           → build simulator (Day 5: LRU+FIFO+LFU)
#   make test4     → build & run Day 4 LRU unit tests
#   make test5     → build & run Day 5 FIFO+LFU+Unified tests
#   make test      → run all tests
#   make run       → build + run simulator
#   make clean     → remove build artifacts

CC     = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
IFLAGS = -Isrc/cache

# ── Source files ────────────────────────────────────────────────────
SRC_CACHE = src/cache/lru.c src/cache/fifo.c src/cache/lfu.c src/cache/cache.c
SRC_MAIN  = src/main.c

.PHONY: all test test4 test5 run clean

all: simulator

simulator: $(SRC_MAIN) $(SRC_CACHE)
	$(CC) $(CFLAGS) $(IFLAGS) -o simulator $^
	@echo "Built: simulator"

test4: tests/test_lru.c src/cache/lru.c
	$(CC) $(CFLAGS) $(IFLAGS) -o test_lru $^
	@echo "Running Day 4 — LRU tests..."
	./test_lru

test5: tests/test_day5.c $(SRC_CACHE)
	$(CC) $(CFLAGS) $(IFLAGS) -o test_day5 $^
	@echo "Running Day 5 — FIFO + LFU + Unified tests..."
	./test_day5

test: test4 test5
	@echo "All tests complete."

run: simulator
	./simulator

clean:
	del /Q simulator.exe test_lru.exe test_day5.exe 2>nul || true
	@echo "Cleaned."
