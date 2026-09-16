# Memory and performance notes — V4

## Collision hot set

Cross-thread position state is no longer stored as separate atomic x/y members inside each `Ant`.

Each read head is represented by one atomic 32-bit packed coordinate:

    packed = (y << 16) | x

The 32-entry position vector is 128 bytes total and 64-byte aligned. A 32-bit enabled mask lets collision lookup skip inactive slots with bit operations rather than loading flags from each `Ant` structure.

This deliberately is **not** occupancy RAM indexed by world cell. It is still just the set of read-head coordinates.

World dimensions are limited to 65535 x 65535 so coordinates remain lossless.

## Scheduler profiling counters

The benchmark now reports:

- instructions
- dispatches
- average executed instructions per dispatch
- average scheduler grant
- empty WFQ scans
- idle waits
- collisions
- population

Benchmark syntax:

    ./tests/bench ANTS QUANTUM SECONDS [WORKERS] [TOKEN_RATE_SCALE]

Examples:

    ./tests/bench 32 256 3 1 1
    ./tests/bench 32 256 3 1 16
    ./tests/bench 32 256 3 2 1

`TOKEN_RATE_SCALE` is for profiling only. It lets us distinguish scheduler overhead caused by token starvation from scheduler overhead incurred while useful work is always available. It does not change the normal universe defaults.

## perf targets

    make perf-stat
    make perf-stat-1w
    make perf-stat-saturated
    make perf-record
    make perf-record-1w
    make perf-record-saturated

The saturated targets use one worker and 16x token generation. Compare them with the normal one-worker profile before changing production token rates.


## V5 occupancy memory
The collision index uses one atomic byte per world cell. At the default 300x200 logical world this is 60 KB, in exchange for O(1) collision lookup. `sizeof(Ant)` remains 40 bytes; the occupancy storage is dynamically allocated and therefore not included in `sizeof(AntColony)`.
