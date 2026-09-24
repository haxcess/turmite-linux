# Development

## Checks

| Command | Coverage |
| --- | --- |
| `make core-test` | Two-worker smoke, final occupancy/population, capacity regression, deterministic mutation |
| `make execution-test` | Independent reference interpreter: library/generated rules, turns, offsets, HALT, tiny worlds |
| `make spawn-test` | Exactly one random library ant; occupied worlds, limits, active leases, recycled slots |
| `make scheduler-deadline-test` | Refill deadlines, long sleeps, extreme scales, pause and recovery |
| `make worker-idle-test` | Idle scan suppression and event/deadline wakeups |
| `make capacity-test` | Dispatch thresholds and draining exemption |
| `make mutation-test` | Collision recovery, blocked residency, HALT, spawn independence; eight-worker stress with population changes/captures |
| `make mutation-tsan-test` | Mutation stress under ThreadSanitizer |
| `make tsan-test` | Core smoke under ThreadSanitizer |
| `make color-offset-test` | All rule sizes, cyclic offsets, unsupported colors, translated Langton traces and randomized spawning and lifecycle |
| `make boundary-test` | Edge crossings, seeded tube orientation, wall overlays, safe single-bit mutation |
| `make debug-drain-test` | Finite token budgets, paused/in-flight work, small balances, HALT/collision recovery |
| `make png-test` | Headless/SDL Q drain; one PNG per rendered frame, finite budget, final-page selection, palettes and export failure |
| `make render-test` | Six-color/code conversion, stable snapshots, invalid buffers |
| `make sdl-test` | Dummy-video/software pixel readback and cleared frames |
| `make multi-window-test` | Frame handoff, controls, restart, close isolation, simulated monitor enumeration |
| `make rule-catalog` | Regenerate browser catalogue/palette |
| `make rule-lab-test` | C/JS traces and generated C entries; requires Node.js |
| `make struct-report` | Host ABI sizes |
| `make bench`, `make perf-stat` | Throughput and hardware counters |

Core tests/benchmark need no SDL; the main executable links SDL and libpng even in headless mode. TSAN requires a supported runtime. `perf` requires counter permissions. Default application/benchmark flags: `-O2 -g`. Dummy-video tests do not validate physical monitor placement or e-ink hardware.

## Source map

| Path | Role |
| --- | --- |
| `src/rules.c`, `src/ant.c` | Catalogue, interpreter, mutation, occupancy |
| `src/scheduler.c` | Credit, tokens, leases, recovery, population, pthread synchronization |
| `src/world.c`, `src/rng.c` | Tape allocation and random streams |
| `src/renderer.{h,c}` | Portable frame conversion |
| `src/main.c` | Controllers, lifecycle, monitors, frame handoff, event routing |
| `src/renderer_sdl.{h,c}` | SDL windows, presentation, HUD |
| `src/dump.c`, `src/png_image.c`, `tools/analyze_dump.py` | Captures and analysis |
| `rule-lab.html`, `tools/rule-*.js` | Rule editor, generator, catalogue |
| `stm32/` | Unintegrated occupancy/port scaffold |

## Manual patches

```sh
git apply --stat /path/to/change.patch
git apply --check /path/to/change.patch
git apply /path/to/change.patch
git diff --check
git diff
```

Stage, commit, and publish manually.

[Experiments](EXPERIMENTS.md) · [Profiling](MEMORY_AND_PERF.md) · [Porting](stm32/INTEGRATION.md)

`make worker-pool-test`: 20 ants across three universes, two workers, lease exclusivity, independent pause/reset, partial thread-creation failure. `make worker-pool-tsan-test`: same workload under ThreadSanitizer.

Boundary stress: build `tests/mutation_stress`, then run it with `bouncy` or
`radioactive` to exercise those box modes with eight workers and concurrent
spawning, halving, and capture. No argument retains the original toroid stress.
