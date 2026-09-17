# Memory and performance

This describes current storage and profiling interfaces. Earlier layouts and measurements are retained in [OPTIMIZATION_NOTES.md](OPTIMIZATION_NOTES.md).

## Per-cell storage

Let `N = width × height` and `P = retained dump pages`. With the current host atomic sizes:

| Storage | Bytes | Allocated in headless mode? |
| --- | ---: | --- |
| Logical six-color tape | `N` | Yes |
| Occupancy index | `N` | Yes |
| Graphical tape snapshot | `N` | No |
| Renderer CPU staging pixels | `4N` | No |
| Raw capture ring | `PN` | Yes, in the main application |

Core storage is about `2N`; the graphical application uses about `(7 + P)N` before SDL-managed textures/framebuffers, metadata, stacks, and allocator overhead. Headless application storage is about `(2 + P)N`. The portable rendering functions allocate no memory themselves. The standalone benchmark and smoke test do not allocate dump rings or renderers.

| World | Core planes (`2N`) | Default raw capture ring (`127N`) |
| --- | ---: | ---: |
| 300×200 benchmark | 120,000 bytes | 7,620,000 bytes |
| 1200×800 windowed/headless default | 1,920,000 bytes | 121,920,000 bytes |
| 1920×1080 | 4,147,200 bytes | 263,347,200 bytes |
| 3840×2160 | 16,588,800 bytes | 1,053,388,800 bytes |

The main application creates a dump ring even when no dump is eventually written. `--dump-pages 1` reduces storage but makes `changed_cells` uninformative in the current implementation. Use at least 3 pages for change/cycle inspection.

The six logical colors fit in three bits, but packing concurrent tape writes would change the independent-byte update protocol. Display transport buffers can use a separate encoding once a display is selected. The core now stores only tape plus occupancy at `2N`; ant writes update only the logical byte. A graphical host owns a stable `N`-byte snapshot, and SDL owns a `4N`-byte conversion buffer. An e-ink backend can map indices to controller codes without RGB, choosing full-frame or tile/row buffers according to its transport needs. See [RENDERING.md](RENDERING.md).

## Ant state and collision lookup

Each occupancy byte is 0 or ant index + 1. Collision discovery is O(1), using compare-and-swap on the destination. This replaced the historical O(32) packed-position scan.

The 32 published positions occupy 128 bytes and are aligned to 64 bytes. Packing is `(y << 16) | x`, limiting dimensions to 65535 per axis. Positions are published at quantum boundaries; occupancy is the live residency index during execution. The enabled mask remains in the colony but no longer drives a collision scan.

The reviewed host build reports `sizeof(Ant)=40`, `sizeof(AntStats)=16`, and `sizeof(AntColony)=1984`. These are ABI-dependent, not STM32 layout guarantees. The colony size includes inline metadata but excludes the allocated occupancy array. Recheck with:

```sh
make struct-report
```

The scheduler retains a scan of at most 32 ants, `double` fair credits, Q16 token balances, and per-ant timestamps. Diagnostic counters include atomic 64-bit fields. Shared pointers and atomic operations need an explicit embedded protocol; host structure sizes alone do not establish port readiness.

## Benchmark

```sh
make tests/bench
./tests/bench ANTS QUANTUM SECONDS [WORKERS] [TOKEN_RATE_SCALE] [MIN_SERVICE]
```

Defaults are `32 256 3 2 1 16`. The benchmark always uses a 300×200 world and seed `0x12345678`, assigning initial rules by ant index. It accepts 1..64 workers, unlike the application's 1..8 limit. Its wall-clock run ends after the requested duration; the application's `--minutes` instead controls repeated universe lifetimes.

Output includes instructions, dispatches, average executed instructions per dispatch, average grant, empty scans, idle waits, collisions, and population, along with configuration. Instructions are totaled across the configured ants; the smoke test's printed instruction count is only ant 0's count.

```sh
./tests/bench 32 256 3 1 1 1     # normal rates, unbatched
./tests/bench 32 256 3 1 1 16    # normal rates, batched
./tests/bench 32 256 3 1 16 16   # 16x rate supply, batched
./tests/bench 32 256 3 2 1 16    # two-worker comparison
```

Rate scaling is a profiling control. It changes token supply and collision health, so saturated runs are workload comparisons, not identical simulations with less scheduler overhead.

## Profiling targets

| Target suffix | Workers | Rate scale | Minimum service |
| --- | ---: | ---: | ---: |
| no suffix | 2 | 1 | 16 |
| `-unbatched` | 1 | 1 | 1 |
| `-1w` | 1 | 1 | 16 |
| `-saturated` | 1 | 16 | 16 |

Each suffix is available for `make perf-stat` and `make perf-record`. All use 32 ants and quantum 256. Stat targets repeat a three-second benchmark five times; record targets run for ten seconds. These targets require `perf` and access to performance counters.

```sh
make perf-stat-unbatched
make perf-stat-1w
make perf-stat-saturated
make perf-record-1w
perf report -i perf-1w.data
```

Recording creates `perf.data`, `perf-unbatched.data`, `perf-1w.data`, or `perf-saturated.data`, depending on the target. `make perf-report` reads only `perf.data`. Benchmark builds include `-O2 -g`; the current default application build omits `-g`.

The checked-in V5/V6 reports and historical throughput figures describe earlier code. Reprofile after this separation: ant instructions no longer write RGB ink, and the Linux render loop now snapshots indices and converts them to fixed-palette pixels.
