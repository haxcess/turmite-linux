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
	rm -f $(OBJ) $(OBJ:.o=.d) $(TARGET)
