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

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -I src -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) $(TARGET) tests/headless_smoke tests/headless_smoke_tsan tests/struct_sizes

.PHONY: core-test tsan-test struct-report

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
