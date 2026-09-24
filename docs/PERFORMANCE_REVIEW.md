# September 2026 performance review

Changes were measured against a saved copy of the working tree before this optimization pass, including the existing collision-mutation option and rule catalog. The machine was an Intel Core Ultra 5 228V with GCC 15.2.0, C17, `-O2 -g`. Measurements were sequential, alternating before/after, three repetitions per case, without CPU pinning. The table reports medians. Raw runs are in [performance-results.json](performance-results.json).

| Measurement | Before | After |
| --- | ---: | ---: |
| Fixed interpreter work, CPU ns/instruction | 16.364 | 11.903 |
| Two stationary ants, wall seconds | 0.742 | 0.263 |
| Saturated 1-worker run, instructions | 93,900,068 | 99,853,609 |
| Saturated 1-worker run, process CPU seconds | 1.792 | 1.496 |
| Rate-limited 2-worker run, instructions | 24,223,645 | 23,929,303 |
| Rate-limited 2-worker run, process CPU seconds | 1.638 | 1.425 |
| Unbatched 1-worker run, instructions | 21,827,900 | 21,630,394 |
| Unbatched 1-worker run, process CPU seconds | 1.378 | 1.422 |

The interpreter used 27% less CPU time per instruction. Every fixed-work run completed 97,280,000 instructions with the same final tape/position/heading/state hash, `e8c616ee383a340f`. These measurements use the original first 38 rules; the catalog was subsequently extended in the shared workspace. `tests/execution_bench 10000 38` retains this comparison; omit the second argument to benchmark the entire current catalog.

The stationary-ant benchmark deliberately stresses adjacent metadata cache lines. Its roughly 2.8× wall-time improvement is not a claim about every moving-ant workload. A 64-byte aligned ant was chosen over the initially compacted 40-byte layout after a separate two-worker comparison showed substantial false sharing.

Timed scheduler workloads change with collisions and wall-clock refill. The rate-limited two-worker case used about 13% less CPU with a small instruction-count decrease; the unbatched case showed no reliable benefit and a roughly 3% median CPU increase. Batching still matters. The fixed-work interpreter measurement is the stronger throughput evidence.

## Idle behavior and memory

A separate 200 ms idle-worker observation recorded 376 scans and 0.003773 process CPU seconds before, versus zero scans and 0.000038 seconds after. Regression tests also verify paused/zero-rate/deadline waits, configuration wakeups, suspend/re-register races, and shutdown. CPU sleep-state residency and electrical power were not measured.

| Host object | Before bytes | After bytes |
| --- | ---: | ---: |
| RuleAction | 12 | 4 |
| TurmiteRule | 312 | 120 |
| Ant | 48 | 64, cache-line aligned |
| AntColony, excluding occupancy | 12,224 | 6,592 |

Colony metadata shrank by 46%. Tape, occupancy, frame buffers, and capture-ring allocations are unchanged.

## Implementation

- A leased worker owns token writes. Per-instruction relaxed stores preserve collision-health visibility while eliminating locked decrements. Flag checks still occur at every instruction boundary.
- Compact rule actions, table-based turn decoding, and carried cell indices reduce hot-loop work. Ant metadata occupies separate cache lines.
- Scheduler scans traverse enabled bits, hoist shared thresholds, and compute refill deadlines only when no ant is runnable. Mutex-owned counters use atomic loads/stores instead of redundant locked increments.
- Workers sleep until refill/recovery deadlines or explicit state-change notifications. Registration plus generation checks prevent lost wakeups. Occupancy reclamation retains a short retry when another recovery may free a cell.
- Refill accounts for long sleeps rather than dropping time after one second; extreme rate products saturate without integer overflow.
- The presenter waits for coalesced frame/completion events. Headless control waits for input or capture/restart deadlines, including closed/invalid stdin. Palette conversion hoists per-pixel alpha preparation.
- The add control now spawns exactly one independently randomized library ant. It checks slot/cell availability and resets reused-slot metadata. Cloning and population doubling were removed.
- Duplicate catalog IDs that blocked the existing mutation test were disambiguated; corresponding rule behavior was preserved. Concurrent catalog and culling edits were retained.

## Validation

Passed: application build; core, mutation/stress, capacity, reference interpreter, spawning, deadlines, idle workers, worker pool, debug drain, color offsets, renderer/SDL, multi-window/multi-monitor, and PNG export checks. The Android native host passed with assertions enabled.

ThreadSanitizer passed worker-pool, idle-wakeup, and eight-worker mutation/capture/population stress. AddressSanitizer plus UndefinedBehaviorSanitizer passed reference execution, spawning, scheduler deadlines, idle workers, and worker lifecycle. No new test weakens existing assertions.

The browser catalog was regenerated. The Node.js C/JS rule-lab suite was not run because Node.js is unavailable. Dummy SDL tests do not validate a physical compositor, monitor timing, or power consumption.

## Reproduction

```sh
make all core-test mutation-test execution-test spawn-test scheduler-deadline-test
make worker-idle-test worker-pool-test debug-drain-test color-test
make render-test sdl-test multi-window-test png-test
make worker-pool-tsan-test mutation-tsan-test
make tests/execution_bench tests/contention_bench tests/bench struct-report
./tests/execution_bench 10000 38
./tests/contention_bench
./tests/bench 32 256 2 1 16 16
./tests/bench 32 256 2 2 1 16
./tests/bench 32 256 2 1 1 1
```
