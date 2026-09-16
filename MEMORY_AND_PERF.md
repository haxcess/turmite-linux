# Memory and Performance Notes

## Current structure layout

The hot `Ant` context is intentionally compact. Scheduler-only token timestamps and cold diagnostic counters live outside `Ant`. On x86-64 the current layout is:

- `sizeof(Ant) = 48` bytes
- `sizeof(AntStats) = 16` bytes
- `sizeof(AntColony) = 2064` bytes

This keeps the execution context small enough that the fixed maximum of 32 ants remains cache-friendly while preserving the forensic counters needed by the dump system.

## Hot-path changes

- One whole token is consumed with a single relaxed atomic RMW; the scheduler grants no more instructions than the ant currently has whole tokens for.
- Ant state and heading are kept in worker-local variables throughout a quantum and published at quantum end (or immediately on HALT).
- Instruction counts are accumulated once per quantum instead of once per instruction.
- Runtime RNG state is per-ant.
- Token timestamps are scheduler-owned rather than stored in every ant.
- Token accrual clamps elapsed time at one second; every configured bucket fills within less than one second, which removes the older multi-division calculation.
- Collision detection remains an O(32) scan by design.
- Scheduler selection remains an O(32) scan by design.

## HALT semantics

A built-in rule may contain a `HALT` action (Busy Beaver currently does). HALT now means the ant becomes non-runnable but remains part of the population. Population decreases only through explicit halving/drain pressure. This keeps the population setpoint stable.

A universe with enough HALTED ants can still become computationally quiescent; the five-minute lifecycle remains the intended stale-universe escape hatch.

## Profiling

The project includes:

```text
make bench
make perf-stat
```

`make perf-stat` runs:

```text
perf stat -d -r 5 ./tests/bench 32 256 3
```

For fair comparisons, use the same seed/configuration and compare several runs. The benchmark reports instruction throughput, dispatches, collisions, and population.

When `perf` is unavailable, the benchmark can still be wrapped with `/usr/bin/time` for coarse CPU-utilization measurements.
