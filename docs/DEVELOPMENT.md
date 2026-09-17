# Development and validation

Run commands from the repository root; source paths below are relative to that root.

## Validation and profiling

```sh
make core-test
make mutation-test
make mutation-tsan-test
make render-test
make sdl-test
make multi-window-test
make tsan-test
make struct-report
make bench
make perf-stat
```

`core-test` runs the smoke, batch-capacity, and deterministic mutation tests. `mutation-test` also runs concurrent stress with eight workers; `mutation-tsan-test` runs that stress under ThreadSanitizer. The smoke test checks population and final occupancy consistency after a short two-worker run. `render-test` checks six-color conversion, a custom panel-code mapping, snapshot isolation, and buffer validation without SDL. `sdl-test` checks actual pixel readback through SDL's dummy video/software renderer. `multi-window-test` checks independent controls, restart, frame ownership, and closing one window while another continues; a second test simulates two detected monitors through the real application entry point. These use SDL dummy video, not physical monitors. ThreadSanitizer requires a supported compiler/runtime. Core tests and the benchmark do not need SDL; the main application still links SDL even in headless mode. The default application and benchmark builds use `-O2 -g`.

For controlled benchmark comparisons, see [EXPERIMENTS.md](EXPERIMENTS.md). Profiling targets require Linux `perf` and permission to use performance counters. The checked-in `perf-*.txt` reports are historical artifacts, not current throughput guarantees.

## Project map and STM32 status

| Path | Responsibility |
| --- | --- |
| `src/rules.c`, `src/ant.c` | Rule catalogue, interpreter, mutation, occupancy operations |
| `src/scheduler.c` | WFQ-like selection, tokens, leases, population controls, pthread synchronization |
| `src/world.c` | Logical tape and allocation |
| `src/renderer.c`, `src/renderer.h` | Portable six-color frame and color conversion |
| `src/main.c` | Monitor discovery, independent controllers/workers, frame handoff, SDL event routing |
| `src/renderer_sdl.c`, `src/renderer_sdl.h` | SDL windows, prepared-pixel presentation, developer HUD |
| `src/dump.c`, `tools/analyze_dump.py` | Logical-world capture and offline inspection |
| `rule-lab.html`, `tools/rule-*.js` | Offline single-ant rule editor, seeded generator, and C export |
| `turmite-ruleTesting.html` | Historical visual playground; its rules differ from the C catalogue |
| `stm32/` | Proposed dual-core design and occupancy helper scaffold |

The existing STM32 design targets H745/H747 with one worker per core and separate FreeRTOS instances. No board, specific panel, Cube project, linker setup, or complete firmware build is present. The display target is six-color e-ink, roughly 8×6 inches; controller, pixel resolution, interface, and refresh requirements remain open. The shared rendering layer is ready for a panel backend, but no e-ink driver is implemented. See [stm32/README.md](stm32/README.md) and [stm32/INTEGRATION.md](stm32/INTEGRATION.md).

[OPTIMIZATION_NOTES.md](history/OPTIMIZATION_NOTES.md) preserves optimization history; [OPTIMIZATION_QUEUE.md](OPTIMIZATION_QUEUE.md) lists remaining work.

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

## Rule catalogue validation

After editing rules or the preview palette, run `make rule-catalog` to refresh the browser data, then `make rule-lab-test` to compare JavaScript execution with C traces and compile generated C entries. The latter requires Node.js (`NODE` can override its command). `make capacity-test` runs the scheduling regression separately. See the [rule lab guide](RULE_LAB.md) for authoring and export details.
