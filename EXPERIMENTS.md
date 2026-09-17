# Experiments with the current prototype

Use explicit dimensions, seed, worker count, and batching settings when comparing runs. Identical seeds do not guarantee identical worlds: timing affects scheduling, tokens, and collisions, including with one worker.

## Quantum and batching

These windowed runs keep dimensions and normal batching fixed while varying the maximum grant:

```sh
./turmite --windowed --width 600 --height 400 --dump-pages 7 --seed 0x12345678 --ants 8 --workers 2 --min-service 16 --quantum 1
./turmite --windowed --width 600 --height 400 --dump-pages 7 --seed 0x12345678 --ants 8 --workers 2 --min-service 16 --quantum 8
./turmite --windowed --width 600 --height 400 --dump-pages 7 --seed 0x12345678 --ants 8 --workers 2 --min-service 16 --quantum 64
./turmite --windowed --width 600 --height 400 --dump-pages 7 --seed 0x12345678 --ants 8 --workers 2 --min-service 16 --quantum 256
```

The effective minimum is `min(min_service, quantum)`. Repeat with `--min-service 1` to isolate unbatched behavior. Watch collisions and visible structure; raw instruction throughput alone does not describe the artwork.

For slow, bursty motion:

```sh
./turmite --windowed --width 600 --height 400 --dump-pages 7 --token-rate-divisor 100 --quantum 64 --min-service 32
```

The divisor changes accrual; minimum service changes burst release. Initial, cloned, and mutated ants still receive full buckets, so slow accrual does not remove their initial burst.

## Population and lifecycle

- `+` / `=` requests doubling to at most 32. Clones inherit phenotype and begin with full buckets at random empty positions.
- `-` requests halving by stopping token generation for weak eligible ants. Leases and reincarnation can affect whether one request reaches the target.
- `SPACE` pauses new dispatches, but not lifecycle time, capture, or already leased work.
- `R` clears and reseeds the universe. `--minutes 0.25` exercises the same restart lifecycle every 15 seconds; it does not exit the process.
- `H` toggles the HUD. There are no ant labels on the world.

Collision counts report newly marked losers. The current instruction loop can continue after displacement; treat immediate collision-stop behavior as an open correctness check, detailed in [SPEC.md](SPEC.md).

## Fixed-palette display and logical captures

Linux displays the logical tape using a fixed six-color palette. There is no drift or per-write ink history; unchanged logical cells retain the same visible color regardless of age. Use `R` to verify that the next frame reflects a fresh universe without retained visual trails.

Use `Q` to write a capture and inspect logical cells independently of the window:

```sh
./turmite --headless --width 300 --height 200 --seed 0x12345678 --dump-pages 31 --dump-interval 0.5
# Type Q followed by Enter to dump and exit.
python3 tools/analyze_dump.py ./turmite-dumps/tape-SEED-TIMESTAMP --page 0
```

Substitute the actual capture directory. Raw dump bytes use the same row-major indices as `RenderFrame`, so they can be mapped through the fixed palette. The analyzer reports statistics; it does not display images. Live captures are non-transactional observations; repeating hashes suggest sampled tape repetition, not deterministic full-state cycles. Avoid capacity 1 for change-count experiments because its previous page is overwritten before comparison.

```sh
make render-test
make sdl-test
```

The first validates portable RGB and custom display-code mappings with no SDL dependency. The second reads pixels back from SDL's dummy video/software renderer, including after a cleared frame. Neither requires a selected e-ink panel or validates panel refresh behavior.

## Scheduler fragmentation and saturation

Build the standalone benchmark, then compare one variable at a time:

```sh
make tests/bench
./tests/bench 32 256 10 1 1 1
./tests/bench 32 256 10 1 1 16
./tests/bench 32 256 10 1 16 16
./tests/bench 32 256 10 2 1 16
```

The first pair compares batching, the next changes token supply, and the final run compares two workers at normal rates. Watch `avg_exec_per_dispatch`, `avg_grant`, `empty_scans`, and `idle_waits`. Tiny grants with few empty scans indicate fragmentation rather than workers repeatedly finding no eligible work.

Equivalent profiling targets include:

```sh
make perf-stat-unbatched
make perf-stat-1w
make perf-stat-saturated
make perf-stat
```

Record compiler, CPU, worker count, and command with results. Saturation changes the workload's token health as well as dispatch behavior. See [MEMORY_AND_PERF.md](MEMORY_AND_PERF.md) for benchmark defaults, output files, and memory costs.
