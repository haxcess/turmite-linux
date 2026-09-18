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
  src/worker_pool.c \
  src/ant.c \
  src/dump.c \
  src/renderer.c \
  src/renderer_sdl.c

OBJ := $(SRC:.c=.o)
TARGET := turmite

.PHONY: all clean mutation-test mutation-tsan-test capacity-test rule-catalog rule-lab-test multi-window-test render-test sdl-test core-test tsan-test struct-report bench perf-stat perf-stat-unbatched perf-stat-1w perf-stat-saturated perf-record perf-record-unbatched perf-record-1w perf-record-saturated perf-report

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -I src -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

bench: tests/bench
	./tests/bench 32 256 3 2 1 16

perf-stat: tests/bench
	perf stat -d -r 5 ./tests/bench 32 256 3 2 1 16

perf-stat-unbatched: tests/bench
	perf stat -d -r 5 ./tests/bench 32 256 3 1 1 1

perf-stat-1w: tests/bench
	perf stat -d -r 5 ./tests/bench 32 256 3 1 1 16

perf-stat-saturated: tests/bench
	perf stat -d -r 5 ./tests/bench 32 256 3 1 16 16


perf-record: tests/bench
	perf record -g -o perf.data -- ./tests/bench 32 256 10 2 1 16

perf-record-unbatched: tests/bench
	perf record -g -o perf-unbatched.data -- ./tests/bench 32 256 10 1 1 1

perf-record-1w: tests/bench
	perf record -g -o perf-1w.data -- ./tests/bench 32 256 10 1 1 16

perf-record-saturated: tests/bench
	perf record -g -o perf-saturated.data -- ./tests/bench 32 256 10 1 16 16

perf-report: perf.data
	perf report -i perf.data

tests/bench: tests/bench.c src/rng.c src/rules.c src/world.c src/ant.c src/scheduler.c
	$(CC) -O2 -g -std=c17 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -I src $^ -pthread -lm -o $@

clean:
	rm -f tests/worker_pool_test tests/worker_pool_test_tsan $(OBJ) $(OBJ:.o=.d) $(TARGET) tests/headless_smoke tests/headless_smoke_tsan tests/struct_sizes tests/bench tests/render_test tests/renderer_sdl_test tests/multi_window_test tests/multi_monitor_app_test tests/scheduler_capacity_test tests/rule_trace tests/rule-traces.js tools/export_rule_catalog tests/generated_rules_check.c tests/generated_rules_check tests/mutation_test tests/mutation_stress tests/mutation_stress_tsan

core-test: tests/headless_smoke tests/scheduler_capacity_test tests/mutation_test
	./tests/headless_smoke
	./tests/mutation_test
	./tests/scheduler_capacity_test

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

# Portable rendering has no SDL or pthread dependency.
render-test: tests/render_test
	./tests/render_test

tests/render_test: tests/render_test.c src/renderer.c src/renderer.h src/colors.h src/world.c src/world.h
	$(CC) $(CFLAGS) -I src tests/render_test.c src/renderer.c src/world.c -o $@

sdl-test: tests/renderer_sdl_test
	SDL_VIDEODRIVER=dummy ./tests/renderer_sdl_test

tests/renderer_sdl_test: tests/renderer_sdl_test.c src/renderer.c src/renderer_sdl.c src/renderer.h src/renderer_sdl.h src/colors.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -I src tests/renderer_sdl_test.c src/renderer.c src/renderer_sdl.c $(shell pkg-config --libs sdl2) -o $@

# Two independent universes/windows on SDL dummy video, without real monitors.
multi-window-test: tests/multi_window_test tests/multi_monitor_app_test
	SDL_VIDEODRIVER=dummy ./tests/multi_window_test
	SDL_VIDEODRIVER=dummy ./tests/multi_monitor_app_test

tests/multi_window_test: tests/multi_window_test.c $(SRC) $(wildcard src/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I src tests/multi_window_test.c $(filter-out src/main.c,$(SRC)) $(LDLIBS) -o $@

tests/multi_monitor_app_test: tests/multi_monitor_app_test.c $(SRC) $(wildcard src/*.h)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I src tests/multi_monitor_app_test.c $(filter-out src/main.c,$(SRC)) $(LDLIBS) -Wl,--wrap=SDL_GetNumVideoDisplays,--wrap=SDL_GetDisplayBounds,--wrap=SDL_CreateWindow,--wrap=SDL_RenderPresent,--wrap=SDL_DestroyWindow,--wrap=SDL_PollEvent,--wrap=renderer_present -o $@

# C regression: large minimum batches must not permanently park small buckets.
capacity-test: tests/scheduler_capacity_test
	./tests/scheduler_capacity_test

tests/scheduler_capacity_test: tests/scheduler_capacity_test.c src/rng.c src/rules.c src/world.c src/ant.c src/scheduler.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -I src $(filter %.c,$^) -pthread -lm -o $@

tools/export_rule_catalog: tools/export_rule_catalog.c src/rules.c src/renderer.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -I src $(filter %.c,$^) -o $@

# Checked-in JS permits offline use without compiling first. Refresh after C edits.
rule-catalog: tools/export_rule_catalog
	./tools/export_rule_catalog > tools/rule-catalog.js.tmp
	mv tools/rule-catalog.js.tmp tools/rule-catalog.js

tests/rule_trace: tests/rule_trace.c src/rng.c src/rules.c src/world.c src/ant.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -I src $(filter %.c,$^) -pthread -lm -o $@

NODE ?= node
rule-lab-test: rule-catalog tests/rule_trace
	./tests/rule_trace > tests/rule-traces.js
	$(NODE) tests/rule_lab_test.js
	$(NODE) tests/export_generated_rules.js > tests/generated_rules_check.c
	$(CC) $(CFLAGS) -I src tests/generated_rules_check.c -o tests/generated_rules_check
	./tests/generated_rules_check

# Deterministic recovery tests plus concurrent ownership/capture stress.
mutation-test: tests/mutation_test tests/mutation_stress
	./tests/mutation_test
	./tests/mutation_stress

tests/mutation_test: tests/mutation_test.c src/scheduler.c src/rng.c src/rules.c src/world.c src/ant.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -I src $(filter-out src/scheduler.c,$(filter %.c,$^)) -pthread -lm -o $@

tests/mutation_stress: tests/mutation_stress.c src/rng.c src/rules.c src/world.c src/ant.c src/scheduler.c src/dump.c $(wildcard src/*.h)
	$(CC) $(CFLAGS) -I src $(filter %.c,$^) -pthread -lm -o $@

mutation-tsan-test: tests/mutation_stress_tsan
	TSAN_OPTIONS=halt_on_error=1 ./tests/mutation_stress_tsan

tests/mutation_stress_tsan: tests/mutation_stress.c src/rng.c src/rules.c src/world.c src/ant.c src/scheduler.c src/dump.c $(wildcard src/*.h)
	$(CC) -O1 -g -std=c17 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -I src -fsanitize=thread $(filter %.c,$^) -pthread -lm -o $@

.PHONY: worker-pool-test worker-pool-tsan-test
POOL_TEST_SRC = tests/worker_pool_test.c src/worker_pool.c src/scheduler.c src/ant.c src/rules.c src/rng.c src/world.c
POOL_TEST_WRAP = -Wl,--wrap=ant_execute_quantum,--wrap=pthread_create
worker-pool-test: tests/worker_pool_test
	./tests/worker_pool_test
tests/worker_pool_test: $(POOL_TEST_SRC) $(wildcard src/*.h)
	$(CC) $(CFLAGS) -I src $(POOL_TEST_SRC) $(POOL_TEST_WRAP) -pthread -lm -o $@
worker-pool-tsan-test:
	$(CC) -O1 -g -std=c17 -D_POSIX_C_SOURCE=200809L -fsanitize=thread -I src $(POOL_TEST_SRC) $(POOL_TEST_WRAP) -pthread -lm -o tests/worker_pool_test_tsan
	TSAN_OPTIONS=halt_on_error=1 ./tests/worker_pool_test_tsan
