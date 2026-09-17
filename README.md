# Turmite Universe — Linux Prototype

A C17 computational artwork: concurrent turmites evolve a shared six-color world under a token-gated, weighted-fair scheduler. Linux is the working reference implementation; the STM32 port is an unintegrated scaffold.

Repository: [haxcess/turmite-linux](https://github.com/haxcess/turmite-linux). Clone URL: `https://github.com/haxcess/turmite-linux.git`.

## Current behavior

- 2, 4, 8, 16, or 32 initial ants; two POSIX worker threads by default.
- Eighteen built-in rules, with up to four internal states and six logical colors.
- A wrapping 2-D world with one relaxed atomic byte per logical cell. A complete turmite instruction is deliberately not a transaction.
- A separate atomic byte per cell records occupancy for O(1) collision lookup. Token balance determines collision health; lower ant ID wins ties.
- Q16 token buckets, weighted-fair credit, configurable quantum, minimum-service batching, and a universe-wide token-rate divisor.
- Population doubling clones eligible ants with full token buckets; halving requests gradual token-drain retirement.
- SDL2 presentation at a target 60 FPS, independent of worker execution, with one world cell per display pixel.
- Platform-independent six-color frame conversion with a fixed Linux preview palette. The core stores only logical colors; SDL owns its RGB output buffers. There is no palette drift or ink history.
- A five-minute software lifecycle stops workers, clears the universe, and starts again with a fresh seed. It is not a hardware watchdog.

See [SPEC.md](SPEC.md) for exact behavior and known limitations.

## Build and run

Dependencies include a C17 compiler, make, pkg-config, pthreads, and SDL2 development files. On Debian/Ubuntu-style systems:

```sh
sudo apt install build-essential pkg-config libsdl2-dev
make
./turmite
```

The default borderless fullscreen window uses SDL display 0. Its reported desktop dimensions determine the world dimensions. Select another monitor with `--display 1` or `--display 2`.

Windowed and headless modes default to a 1200×800 world and accept explicit dimensions:

```sh
./turmite --windowed --width 1200 --height 800
./turmite --display 1 --ants 16 --quantum 64 --seed 0x12345678
./turmite --windowed --token-rate-divisor 100 --min-service 32
./turmite --headless --width 300 --height 200 --dump-pages 7
./turmite --help
```

The application defaults to 8 ants, 2 workers, quantum 32, minimum service 16, and token-rate divisor 1. It accepts 1..8 workers and quantum/minimum-service values of 1..4096. A minimum service of 1 reproduces unbatched scheduling. `--minutes` changes the restart interval; it does not make the process exit after that duration.

## Runtime controls

These controls apply to the SDL window:

| Key | Action |
| --- | --- |
| `SPACE` | Pause/resume dispatch |
| `[` / `]` | Decrease/increase quantum |
| `-` | Request halving through token-drain retirement |
| `+` or `=` | Request doubling, up to 32 ants |
| `R` | Start a fresh universe with a new seed |
| `H` | Toggle the developer HUD |
| `Q` | Stop workers, capture a final page, write a debug dump, and quit |
| `ESC` | Quit without a dump |

Use `--no-hud` to start without the HUD. Headless input supports only `Q`/`q` followed by Enter for dump-and-quit. Pause does not freeze the lifecycle timer or capture schedule, and an already leased quantum can finish. Restart restores the original configured quantum and population.

## Concurrency and presentation

The scheduler mutex protects leases and fair-credit bookkeeping; each ant can be leased by only one worker at a time. Workers execute outside that mutex. Occupancy updates use compare-and-swap; tape writes use independent relaxed byte stores. Workers do no rendering work.

Workers keep position, heading, and state local during a quantum and publish them at its end. The occupancy array is the live residency index; published positions are diagnostic and lifecycle snapshots. Collision losers are marked clobbered/displaced, and scheduler reincarnation requires reclaiming their published position. The current executor still finishes its granted quantum after a failed movement claim; see the collision limitation in [SPEC.md](SPEC.md).

An explicit seed defines initialization and RNG streams, but timing and thread interleaving can make repeated runs diverge. A dump is observational, not a replay checkpoint.

The host samples the tape into a stable, row-major frame of indices 0..5. Portable rendering maps those indices to a fixed RGB palette for SDL or caller-supplied display codes. The SDL backend receives frame and HUD snapshots without accessing simulation objects. Headless runs allocate no display snapshot or RGB buffers. See [RENDERING.md](RENDERING.md) for the platform boundary.

## Debug dumps

Capture runs in graphical and headless modes. The default ring retains 127 logical-world pages sampled once per second, with an initial capture and an additional final capture on debug quit. Accepted capacities are `1,3,7,15,31,63,127,255,511,1023`. Storage uses exactly that many pages and modulo indexing.

```sh
./turmite --windowed --dump-pages 31 --dump-interval 0.5 --dump-dir ./captures
```

Dumps are written under `./turmite-dumps/` by default and contain:

- `manifest.txt`: dimensions, seed, scheduler settings, chronological page hashes/change counts, and ant metadata.
- `pages/page-NNNN.bin`: one byte per logical cell, row-major, values 0..5.
- `README.txt`: raw-page format notes.

Captures contain logical indices that can be rendered through the same fixed palette; they contain no platform-specific pixels. Live samples are assembled while workers run, so a page and its metadata are not a globally frozen instant. Hashes use FNV-1a. With capacity 1, `changed_cells` is always zero because the previous page has already been overwritten. Per-ant RNG states and the global token-rate divisor are not recorded.

Inspect a dump with Python 3:

```sh
python3 tools/analyze_dump.py ./turmite-dumps/tape-SEED-TIMESTAMP
python3 tools/analyze_dump.py ./turmite-dumps/tape-SEED-TIMESTAMP --page 0
```

Replace the example directory with an actual capture path. Reported repeating hashes are clues about sampled tape states, not proof that the whole simulation is cycling.

Memory grows with world resolution: the core currently uses about 2 bytes per cell, plus 1 byte per cell for the graphical snapshot and 4 bytes per cell for the SDL staging buffer, SDL-managed resources, and 1 byte per cell per retained dump page. At 1920×1080, the default dump pages alone require about 263 MB; at 3840×2160, about 1.05 GB. See [MEMORY_AND_PERF.md](MEMORY_AND_PERF.md) for the breakdown.

## Validation and profiling

```sh
make core-test
make render-test
make sdl-test
make tsan-test
make struct-report
make bench
make perf-stat
```

The core test checks population and final occupancy consistency after a short two-worker run. `render-test` checks six-color conversion, a custom panel-code mapping, snapshot isolation, and buffer validation without SDL. `sdl-test` checks actual pixel readback through SDL's dummy video/software renderer. ThreadSanitizer requires a supported compiler/runtime. Core tests and the benchmark do not need SDL; the main application still links SDL even in headless mode. The default application build uses `-O2` without `-g`; benchmark/profile builds include debug information.

For controlled benchmark comparisons, see [EXPERIMENTS.md](EXPERIMENTS.md). Profiling targets require Linux `perf` and permission to use performance counters. The checked-in `perf-*.txt` reports are historical artifacts, not current throughput guarantees.

## Project map and STM32 status

| Path | Responsibility |
| --- | --- |
| `src/rules.c`, `src/ant.c` | Rule catalogue, interpreter, mutation, occupancy operations |
| `src/scheduler.c` | WFQ-like selection, tokens, leases, population controls, pthread synchronization |
| `src/world.c` | Logical tape and allocation |
| `src/renderer.c`, `src/renderer.h` | Portable six-color frame and color conversion |
| `src/main.c` | Linux lifecycle, input, workers, frame/HUD sampling |
| `src/renderer_sdl.c`, `src/renderer_sdl.h` | SDL transport, RGB buffers, developer HUD |
| `src/dump.c`, `tools/analyze_dump.py` | Logical-world capture and offline inspection |
| `turmite-ruleTesting.html` | Standalone browser rule playground; not the concurrent C runtime |
| `stm32/` | Proposed dual-core design and occupancy helper scaffold |

The existing STM32 design targets H745/H747 with one worker per core and separate FreeRTOS instances. No board, specific panel, Cube project, linker setup, or complete firmware build is present. The display target is six-color e-ink, roughly 8×6 inches; controller, pixel resolution, interface, and refresh requirements remain open. The shared rendering layer is ready for a panel backend, but no e-ink driver is implemented. See [stm32/README.md](stm32/README.md) and [stm32/INTEGRATION.md](stm32/INTEGRATION.md).

[OPTIMIZATION_NOTES.md](OPTIMIZATION_NOTES.md) preserves optimization history; [OPTIMIZATION_QUEUE.md](OPTIMIZATION_QUEUE.md) lists remaining work.

## Change workflow

Changes can be delivered as Git patch files for manual review and application. From the repository root:

```sh
git apply --stat /path/to/change.patch
git apply --check /path/to/change.patch
git apply /path/to/change.patch
git diff --check
git diff
```

Applying a patch changes working files; it does not stage, commit, or publish them. Publishing to GitHub is a manual maintainer step.
