CC ?= cc
CFLAGS ?= -O2 -g -std=c17 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L
CPPFLAGS += $(shell pkg-config --cflags sdl2 2>/dev/null)
LDLIBS += $(shell pkg-config --libs sdl2 2>/dev/null)
LDLIBS += -pthread -lm

SRC := \
  src/main.c \
  src/rng.c \
  src/rules.c \
  src/world.c \
  src/scheduler.c \
  src/ant.c \
  src/dump.c \
  src/renderer_sdl.c

OBJ := $(SRC:.c=.o)
TARGET := turmite

.PHONY: all clean core-test tsan-test struct-report bench perf-stat perf-stat-1w perf-stat-saturated perf-record perf-record-1w perf-record-saturated perf-report

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -I src -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

bench: tests/bench
	./tests/bench 32 256 3

perf-stat: tests/bench
	perf stat -d -r 5 ./tests/bench 32 256 3 2 1

perf-stat-1w: tests/bench
	perf stat -d -r 5 ./tests/bench 32 256 3 1 1

perf-stat-saturated: tests/bench
	perf stat -d -r 5 ./tests/bench 32 256 3 1 16


perf-record: tests/bench
	perf record -g -o perf.data -- ./tests/bench 32 256 10 2 1

perf-record-1w: tests/bench
	perf record -g -o perf-1w.data -- ./tests/bench 32 256 10 1 1

perf-record-saturated: tests/bench
	perf record -g -o perf-saturated.data -- ./tests/bench 32 256 10 1 16

perf-report: perf.data
	perf report -i perf.data

tests/bench: tests/bench.c src/rng.c src/rules.c src/world.c src/ant.c src/scheduler.c
	$(CC) -O2 -g -std=c17 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -I src $^ -pthread -lm -o $@

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) $(TARGET) tests/headless_smoke tests/headless_smoke_tsan tests/struct_sizes tests/bench

core-test: tests/headless_smoke
	./tests/headless_smoke

tests/headless_smoke: tests/headless_smoke.c src/rng.c src/rules.c src/world.c src/ant.c src/scheduler.c
	$(CC) -O2 -g -std=c17 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -I src $^ -pthread -lm -o $@

tsan-test: tests/headless_smoke_tsan
	TSAN_OPTIONS=halt_on_error=1 ./tests/headless_smoke_tsan

tests/headless_smoke_tsan: tests/headless_smoke.c src/rng.c src/rules.c src/world.c src/ant.c src/scheduler.c
	$(CC) -O1 -g -std=c17 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -I src -fsanitize=thread $^ -pthread -lm -o $@

struct-report: tests/struct_sizes
	./tests/struct_sizes

tests/struct_sizes: tests/struct_sizes.c src/rules.c src/rules.h
	$(CC) -O2 -std=c17 -I src tests/struct_sizes.c src/rules.c -o $@
