# Suggested First Experiments

Run the same explicit seed under different scheduler parameters and compare the resulting worlds.

```sh
./turmite --seed 0x12345678 --ants 8 --quantum 1
./turmite --seed 0x12345678 --ants 8 --quantum 8
./turmite --seed 0x12345678 --ants 8 --quantum 64
./turmite --seed 0x12345678 --ants 8 --quantum 256
```

Then try population changes during execution:

- `+` / `=` doubles the population up to 32.
- `-` begins token-drain expiration until the population is halved.

Useful things to watch are collision rate, mutation frequency, and whether a scheduler/quantum combination produces qualitatively different structures from the same starting seed.

The development HUD can be hidden with `H`; the universe itself never draws ant labels or textual information.

## Scheduler token-fragmentation experiment

Compare these two single-worker profiles:

```bash
make perf-stat-1w
make perf-stat-saturated
```

Or run directly:

```bash
./tests/bench 32 256 10 1 1
./tests/bench 32 256 10 1 16
```

Watch `avg_exec_per_dispatch`, `avg_grant`, `empty_scans`, and `idle_waits`.

If empty scans remain near zero but average grants are tiny, the scheduler is finding work but dispatching it in very small fragments. This distinguishes token fragmentation from polling starvation.
