# RTOS Task Scheduler — build, test and analysis entry points.
#
#   make            release build of the simulator
#   make test       build and run the unit test suite
#   make run        build and run with default configuration
#   make debug      -O0 -g3 build
#   make asan       AddressSanitizer + UBSan build, tests and a short run
#   make tsan       ThreadSanitizer build, tests and a short run
#   make analyze    GCC static analyzer over the whole program
#   make clean      remove build artefacts

CC       ?= cc
CSTD     ?= -std=c11
OPT      ?= -O2
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Werror
CPPFLAGS := -Iinclude
LDLIBS   := -lm -lpthread
LDFLAGS  ?=

BUILD    ?= build
TARGET   := $(BUILD)/rtos_scheduler
TEST_BIN := $(BUILD)/run_tests

LIB_SRCS  := $(filter-out src/main.c,$(wildcard src/*.c))
APP_SRCS  := src/main.c
TEST_SRCS := $(wildcard tests/*.c)

LIB_OBJS  := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(LIB_SRCS))
APP_OBJS  := $(patsubst src/%.c,$(BUILD)/obj/%.o,$(APP_SRCS))
TEST_OBJS := $(patsubst tests/%.c,$(BUILD)/obj/tests/%.o,$(TEST_SRCS))

CFLAGS := $(CSTD) $(WARN) $(OPT) $(CPPFLAGS)
DEPS   := $(LIB_OBJS:.o=.d) $(APP_OBJS:.o=.d) $(TEST_OBJS:.o=.d)

all: $(TARGET)

$(BUILD)/obj/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/obj/tests/%.o: tests/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I tests -MMD -MP -c $< -o $@

$(TARGET): $(LIB_OBJS) $(APP_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(TEST_BIN): $(LIB_OBJS) $(TEST_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

run: $(TARGET)
	./$(TARGET) $(ARGS)

# Sanitizer and analysis builds use their own object trees. Each target
# re-invokes make with an overridden BUILD/OPT so the object directory and the
# compiler flags stay in sync for every prerequisite.
ASAN_OPT     := -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
ASAN_LDFLAGS := -fsanitize=address,undefined
TSAN_OPT     := -O1 -g -fsanitize=thread
TSAN_LDFLAGS := -fsanitize=thread

debug:
	$(MAKE) BUILD=build/debug OPT="-O0 -g3 -fno-omit-frame-pointer" all test

asan:
	$(MAKE) BUILD=build/asan OPT="$(ASAN_OPT)" LDFLAGS="$(ASAN_LDFLAGS)" all test
	ASAN_OPTIONS=detect_leaks=1 ./build/asan/rtos_scheduler --duration 3000 --quiet

tsan:
	$(MAKE) BUILD=build/tsan OPT="$(TSAN_OPT)" LDFLAGS="$(TSAN_LDFLAGS)" all test
	./build/tsan/rtos_scheduler --duration 3000 --quiet

analyze:
	$(MAKE) BUILD=build/analyze OPT="-O2 -fanalyzer -fno-diagnostics-show-caret" all

clean:
	rm -rf build rtos_scheduler

help:
	@sed -n '1,12p' Makefile | sed 's/^# \{0,1\}//'

.PHONY: all test run debug asan tsan analyze clean help

-include $(DEPS)
