# Turmite Universe — Linux Prototype

A deliberately small C17 reference implementation of the Turmite Universe concept.

The prototype keeps the machine's important behaviors intact while using Linux as the substrate:

- C17
- POSIX pthreads
- C11/C17 atomics
- Linux's native scheduler is left alone
- two logical worker threads by default
- 2/4/8/16/32 logical ants
- six-color shared 2-D world
- relaxed atomic world cells, with intentionally non-transactional read/modify/write steps
- compact weighted-fair/deficit-like ant scheduler
- per-ant token buckets; one token buys one turmite instruction
- configurable execution quantum
- collisions resolved by accumulated-token health
- collision mutation preserves position but randomizes rule/heading/state/scheduling parameters
- doubling clones ants at random positions
- halving applies scheduler/resource pressure and lets ants expire
- Galois LFSR pseudo-random generator seeded from host entropy or an explicit seed
- SDL2 display at a throttled 60 FPS, with 4x4 pixel cells
- optional development HUD
- five-minute prototype watchdog lifecycle (configurable for testing)

## Build

Dependencies on Debian/Ubuntu-style systems:

```sh
sudo apt install build-essential pkg-config libsdl2-dev
```

Then:

```sh
make
./turmite
```

Useful examples:

```sh
./turmite --ants 16 --quantum 16 --seed 0x12345678
./turmite --ants 32 --quantum 64 --workers 2 --width 1200 --height 800
./turmite --minutes 0.25
```

`--minutes` is mainly useful while developing the lifecycle code. The intended appliance behavior is 5 minutes.

## Runtime controls

- `SPACE` pause/resume
- `[` / `]` decrease/increase quantum
- `-` halve the population
- `+` or `=` double the population
- `R` create a fresh universe (new seed)
- `H` toggle the developer HUD
- `Q` debug quit: stop the universe and write a headless paper-tape dump
- `ESC` quit without a dump

The scheduler policy is currently fixed to weighted-fair scheduling. Its selection is part of the universe configuration interface, but only WFQ is implemented in this first prototype.

## Deliberate concurrency notes

The scheduler protects ant ownership and dispatch state. The world does not have cell locks. World cells are C atomics so the program stays within the C memory model while still permitting independent read/compute/write races. A turmite's three-step transition is not a transaction.

Ant positions are atomic too, because workers may inspect them during collision detection. There is deliberately no occupancy map. Collision discovery is therefore a concurrent observation of the ant contexts and may be timing-sensitive.

This is a reference experiment, not a deterministic cellular-automaton simulator. The same seed and configuration can still diverge because Linux scheduling and thread interleaving are outside the machine's control.

## Debug dumps

Press `Q` to stop the current universe and write a headless capture under `./turmite-dumps/` by default. The capture is a rolling history of whole-world pages plus ant/scheduler metadata; it does not use SDL to create the dump.

The default is 127 retained pages, sampled once per second. Page counts are `1,3,7,15,31,63,127,255,511,1023` (2^n-1), so the ring-buffer indexing can use a power-of-two-sized storage calculation while retaining the requested page count. Configure this with:

```sh
./turmite --dump-pages 255 --dump-interval 0.5 --dump-dir ./captures
```

Each dump contains:

- `manifest.txt`: seed, world dimensions, scheduler settings, per-page age/hash/change count, and per-ant state/rule/token metadata
- `pages/page-NNNN.bin`: raw 8-bit color-index world pages, row-major, one byte per cell
- `README.txt`: raw-page format notes

`world_hash_hex` is FNV-1a over each raw page, and `changed_cells` compares a page with the preceding captured page. These are intended to make stagnant or cycling universes easy to spot without the SDL window.

## Headless / performance work

The reference prototype can now run without opening an SDL window:

```bash
./turmite --headless --ants 32 --quantum 64 --dump-pages 127
```

Type `Q` followed by Enter to stop and write the rolling debug dump. The headless control thread sleeps while the worker threads burn through the universe.

The first optimization pass also reduces the hot-path cost and ant footprint: Q16 token accounting replaces per-instruction floating-point accumulation, runtime RNG becomes per-ant, scheduler quantum is lock-free to read, and ant execution keeps x/y local between world moves. The world remains one atomic byte per cell because byte-level atomic last-write-wins semantics are part of the experiment.

Useful validation targets:

```bash
make core-test
make tsan-test
make struct-report
make bench
make perf-stat
```

For Linux CPU profiling:

```bash
perf stat -d ./turmite --headless --ants 32 --quantum 64 --minutes 0.25 --dump-pages 1
perf record -g ./turmite --headless --ants 32 --quantum 64 --minutes 0.25 --dump-pages 1
perf report
```


## Fedora perf

On Fedora 44, the kernel `perf` tool is packaged as `perf`. Install it with:

```bash
sudo dnf install perf
```

Then run:

```bash
make perf-stat
```

The Fedora package listing confirms `perf` is available for Fedora 44.

## V4 profiling notes

V4 packs all 32 cross-thread ant positions into a 128-byte, cache-aligned array plus a 32-bit enabled mask. This is intended to remove the old 48-byte-stride collision scan seen in `perf report` while preserving the no-cell-occupancy-map design.

The benchmark accepts optional worker-count and token-rate-scale parameters:

```bash
./tests/bench 32 256 3 1 1    # one worker, normal rates
./tests/bench 32 256 3 1 16   # one worker, deliberately saturated token supply
```

Use the saturated run only to isolate scheduler cost; normal universe behavior still uses the original randomized per-ant token rates.

## V5 collision index

V5 replaces the O(32) read-head collision scan with a derived O(1) occupancy index. The six-color tape is unchanged. Ant position remains authoritative; the occupancy table is one atomic byte per logical world cell and stores only the current resident ant id.

A collision loser is marked displaced and clobbered. It retains its published position but cannot reincarnate until that position can be reclaimed after the winner leaves. This gives the winner escape time while preserving one resident read-head per cell.

Use the existing profiling targets to compare against V4:

    make perf-record-1w
    make perf-record-saturated

