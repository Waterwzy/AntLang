# Build antc with a C99 compiler (gcc, clang, ...).
CC      ?= cc
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -Wpedantic
LDFLAGS ?=
SRC      = src/main.c src/parse.c src/sim.c src/codegen.c src/util.c
HDR      = src/antc.h
BIN      = antc

$(BIN): $(SRC) $(HDR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

.PHONY: clean test
clean:
	rm -f $(BIN) $(BIN).exe
	rm -rf build

test: $(BIN)
	python3 tests/run_tests.py
