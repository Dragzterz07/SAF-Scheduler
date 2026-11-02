CC ?= gcc
CFLAGS ?= -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -g -pthread -Iinclude -Isrc

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CFLAGS += -D_DARWIN_C_SOURCE -D_XOPEN_SOURCE=600 -Wno-deprecated-declarations -DMAP_STACK=0 -Wno-unused-parameter
endif

DEBUG ?= 1
ifeq ($(DEBUG),0)
    CFLAGS += -O2 -DNDEBUG
endif

TARGET = scheduler
SRC = $(wildcard src/*.c)
LIB_OBJS = $(patsubst src/%.c, build/%.o, $(SRC))

MAIN_SRC = main.c
MAIN_OBJ = build/main.o

TEST_SRCS = $(wildcard tests/test_*.c)
TEST_BINS = $(patsubst tests/%.c, build/tests/%, $(TEST_SRCS))

# TEMP: rule for tcb.cpp removed in task 03 (converted to src/tcb.c)

all: $(TARGET) $(TEST_BINS)

$(TARGET): $(MAIN_OBJ) $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/main.o: $(MAIN_SRC) | build
	$(CC) $(CFLAGS) -c $< -o $@

build/tests/%.o: tests/%.c | build/tests
	$(CC) $(CFLAGS) -c $< -o $@

build/tests/%: build/tests/%.o $(LIB_OBJS)
	$(CC) $(CFLAGS) -o $@ $^

build:
	mkdir -p build

build/tests:
	mkdir -p build/tests

test: $(TARGET) $(TEST_BINS)
	@for t in $(TEST_BINS); do \
		echo "Running $$t..."; \
		./$$t || exit 1; \
	done

test-mem: $(TARGET) $(TEST_BINS)
	@chmod +x scripts/test_mem.sh
	./scripts/test_mem.sh

test-long: build/tests/test_mixed
	SAF_MIXED_TICKS=30000 ./build/tests/test_mixed

clean:
	rm -rf build $(TARGET)

.PHONY: all test test-mem test-long clean
