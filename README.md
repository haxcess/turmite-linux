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
