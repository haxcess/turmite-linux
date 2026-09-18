# Development

## Checks

| Command | Coverage |
| --- | --- |
| `make core-test` | Two-worker smoke, final occupancy/population, capacity regression, deterministic mutation |
| `make capacity-test` | Dispatch thresholds and draining exemption |
| `make mutation-test` | Collision recovery, blocked residency, HALT, clone isolation; eight-worker stress with population changes/captures |
| `make mutation-tsan-test` | Mutation stress under ThreadSanitizer |
| `make tsan-test` | Core smoke under ThreadSanitizer |
| `make render-test` | Six-color/code conversion, stable snapshots, invalid buffers |
| `make sdl-test` | Dummy-video/software pixel readback and cleared frames |
| `make multi-window-test` | Frame handoff, controls, restart, close isolation, simulated monitor enumeration |
| `make rule-catalog` | Regenerate browser catalogue/palette |
| `make rule-lab-test` | C/JS traces and generated C entries; requires Node.js |
| `make struct-report` | Host ABI sizes |
| `make bench`, `make perf-stat` | Throughput and hardware counters |

Core tests/benchmark need no SDL; the main executable links SDL even in headless mode. TSAN requires a supported runtime. `perf` requires counter permissions. Default application/benchmark flags: `-O2 -g`. Dummy-video tests do not validate physical monitor placement or e-ink hardware.

## Source map

| Path | Role |
| --- | --- |
| `src/rules.c`, `src/ant.c` | Catalogue, interpreter, mutation, occupancy |
| `src/scheduler.c` | Credit, tokens, leases, recovery, population, pthread synchronization |
| `src/world.c`, `src/rng.c` | Tape allocation and random streams |
| `src/renderer.{h,c}` | Portable frame conversion |
| `src/main.c` | Controllers, lifecycle, monitors, frame handoff, event routing |
| `src/renderer_sdl.{h,c}` | SDL windows, presentation, HUD |
| `src/dump.c`, `tools/analyze_dump.py` | Captures and analysis |
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
