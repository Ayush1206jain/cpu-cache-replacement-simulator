# CPU Cache Replacement Simulator — Makefile
#
# Usage:
#   make           → build simulator
#   make test      → build & run unit tests
#   make clean     → remove build artifacts
#   make run       → build + run simulator
#
# Compiler: gcc (mingw-w64 or native Linux/macOS)

CC      = gcc
CFLAGS  = -Wall -Wextra -std=c11 -O2
IFLAGS  = -Isrc/cache

# ── Source files ───────────────────────────────────────────────────
SRC_CACHE  = src/cache/lru.c
SRC_MAIN   = src/main.c

# ── Targets ────────────────────────────────────────────────────────
.PHONY: all test clean run

all: simulator

simulator: $(SRC_MAIN) $(SRC_CACHE)
	$(CC) $(CFLAGS) $(IFLAGS) -o simulator $(SRC_MAIN) $(SRC_CACHE)
	@echo "Built: simulator"

test: tests/test_lru.c $(SRC_CACHE)
	$(CC) $(CFLAGS) $(IFLAGS) -o test_lru tests/test_lru.c $(SRC_CACHE)
	@echo "Running LRU unit tests..."
	./test_lru

run: simulator
	./simulator

clean:
	del /Q simulator.exe test_lru.exe 2>nul || true
	@echo "Cleaned build artifacts"
