# Experiments

Record revision, compiler, CPU, dimensions, seed, workers, and command. Repeat measurements: identical seeds do not reproduce timing, even with one worker.

## Batching and contention

```sh
make tests/bench
./tests/bench 32 256 10 1 1 1
./tests/bench 32 256 10 1 1 16
./tests/bench 32 256 10 1 16 16
./tests/bench 32 256 10 2 1 16
```

Compare batching, rate saturation, then worker count. Inspect `avg_exec_per_dispatch`, `avg_grant`, `empty_scans`, `idle_waits`. Tiny grants with few empty scans indicate fragmentation. Saturation also changes collision health.

Equivalent perf targets: `perf-stat-unbatched`, `perf-stat-1w`, `perf-stat-saturated`, `perf-stat`. See [benchmark reference](MEMORY_AND_PERF.md).

## Visual batches

```sh
./turmite -W -p 0 --width 600 --height 400 --dump-pages 7 --seed 0x12345678 --ants 8 --workers 2 --min-service 16 --quantum 64
```

Repeat with quantum 2, 8, 64, 256; then minimum service 1. For slow bursts, use `--token-rate-divisor 100 --quantum 64 --min-service 32`. Startup, new spawns, and HALT rebirth receive full buckets; collision mutation preserves balance.

## Manual checks

| Setup / action | Check |
| --- | --- |
| `./turmite -A --dump-pages 7` | One fullscreen universe per physical monitor |
| `./turmite -F -p 1 -u --dump-pages 7` | Selected monitor and HUD |
| Focus one window; Space, H, R, Esc | Controls and close remain local |
| `+`, `-` | Spawn one random library ant; request drain retirement |
| `--minutes 0.25` | Restart every 15 seconds; process continues |
| R | Fresh tape, no retained visual history |

Automated dummy-video coverage: `make multi-window-test`. Lifecycle/mutation coverage: `make mutation-test`.

## Capture

```sh
./turmite --headless --width 300 --height 200 --seed 0x12345678 --dump-pages 31 --dump-interval 0.5
# Q then Enter
python3 tools/analyze_dump.py ./turmite-dumps/tape-SEED-TIMESTAMP --page 0
```

Use the actual output directory. [Dump limitations](DEBUGGING.md) apply; repeating tape hashes do not establish full-state cycles.
