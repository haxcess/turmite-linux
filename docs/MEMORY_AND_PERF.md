# Memory and performance

## Storage

Per universe: `N = logical_width × logical_height` (canvas dimensions divided by `--cell-size`, rounded down), `P = capture pages`. Current host atomic sizes:

| Allocation | Bytes | Headless |
| --- | ---: | --- |
| Logical tape | N | Yes |
| Occupancy | N | Yes |
| Index snapshot | N | No |
| Three ARGB frames | 12N | No |
| Capture ring | PN | Yes |

Core: `2N`. Graphical application: `(15 + P)N`. Headless application: `(2 + P)N`. Excludes SDL resources, metadata, stacks, allocator overhead. Sum across universes; worker stacks belong to one process-wide pool. Standalone benchmark/smoke omit captures and rendering.

| Dimensions | Core bytes | Default ring bytes (P=127) |
| --- | ---: | ---: |
| 300 × 200 | 120,000 | 7,620,000 |
| 1200 × 800 | 1,920,000 | 121,920,000 |
| 1920 × 1080 | 4,147,200 | 263,347,200 |
| 3840 × 2160 | 16,588,800 | 1,053,388,800 |

Rings allocate at startup. One-page rings minimize memory but cannot report useful deltas. Packing tape into three bits would require read-modify-write synchronization; panel transport may use separate packing.

Host ABI measurements (`make struct-report`):

| Object | Bytes |
| --- | ---: |
| `Ant` | 40 |
| `AntStats` | 16 |
| `AntColony` | 11,968, excluding occupancy allocation |
| 32 private rules, included in colony | 9,984 |
| 32 packed positions | 128; 64-byte alignment |
| Scheduler recovery timestamps | 256 |

Sizes are ABI-dependent. Embedded pointers, atomics, and 64-bit diagnostics require a separate shared-state protocol.

## Benchmark

```sh
make tests/bench
./tests/bench ANTS QUANTUM SECONDS [WORKERS] [TOKEN_RATE_SCALE] [MIN_SERVICE]
```

Defaults: `32 256 3 2 1 16`; 300 × 200 world; seed `0x12345678`; initial rules by ant index; 1–64 workers. Exits after SECONDS, unlike application restart intervals.

The benchmark uses the standalone single-universe scheduler. In the graphical application, dispatch/grant counters remain per universe; idle/empty counters count unsuccessful global scans while that universe is registered. Dump/HUD worker count is shared pool capacity.

Reports total instructions, dispatches, average execution/grant, empty scans, idle waits, collisions, population, and configuration. Smoke-test instruction output counts only ant 0. Rate scaling changes collision health and workload as well as dispatch supply.

## Profiling

| Suffix | Workers | Rate scale | Min service |
| --- | ---: | ---: | ---: |
| none | 2 | 1 | 16 |
| `-unbatched` | 1 | 1 | 1 |
| `-1w` | 1 | 1 | 16 |
| `-saturated` | 1 | 16 | 16 |

Suffixes apply to `make perf-stat` and `make perf-record`. All use 32 ants, quantum 256. Stat: five 3-second runs. Record: one 10-second run, output `perf[SUFFIX].data`. `make perf-report` reads only `perf.data`.

```sh
make perf-record-1w
perf report -i perf-1w.data
```

Requires Linux perf/counter permissions. Builds use `-O2 -g`. Benchmark excludes rendering and aggregate multi-monitor costs. Checked-in perf reports are [historical](history/OPTIMIZATION_NOTES.md).

[Comparison procedures](EXPERIMENTS.md)
