#!/bin/sh
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd -P)
cd "$repo"
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
${CC:-cc} -O2 -std=c17 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -DESP_PLATFORM \
    -I src -c src/scheduler.c -o "$test_dir/scheduler.o"
${CC:-cc} -O2 -std=c17 -Wall -Wextra -D_POSIX_C_SOURCE=200809L -I src \
    esp32/tests/scheduler_port_test.c "$test_dir/scheduler.o" \
    src/ant.c src/world.c src/rules.c src/rng.c -pthread -lm \
    -Wl,--wrap=pthread_condattr_init -o "$test_dir/scheduler_test"
"$test_dir/scheduler_test"
python3 esp32/tests/launcher_test.py
