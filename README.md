# Turmite Universe

An academic art piece and an experiment in multithreading, evolution, and cellular automata. Concurrent turmites paint a shared six-color canvas: simple local rules meet thread timing, competition, and mutation to produce changing patterns.

The project's technical contributions are an experimental implementation of asynchronous, shared-world cellular automata: fair scheduling across worker threads, local rule evolution through collisions and HALT rebirth, and fast simulation through batched execution and constant-time collision lookup. Simulation runs independently of display refresh, letting computational activity shape the artwork at its own pace.

## Build and run

Requires a C17 compiler, make, pkg-config, pthreads, and SDL2 development files.

```sh
make
./turmite
```

The default is one 1200 × 800 window on display 0, with the HUD hidden.

```sh
./turmite --fullscreen
./turmite --fullscreen-all
./turmite --ants 4 --workers 4 --quantum 790 --min-service 1000
./turmite --help
```

| Option | Purpose |
| --- | --- |
| `-W`, `--windowed` | Normal window (default) |
| `-p N`, `--display N` | Select display for a single window |
| `-F`, `--fullscreen` | Fullscreen on the selected display |
| `-A`, `--fullscreen-all` | Independent fullscreen universe on every monitor |
| `-u`, `--hud` | Show the HUD |
| `--ants N`, `--workers N` | Set population and worker threads per universe |
| `--quantum N`, `--min-service N` | Tune execution batches |
| `-v N`, `--token-rate-divisor N` | Slow token refill by a divisor |
| `--minutes N` | Restart each universe after N minutes (default 5) |

Press **Esc** to close a window. See [running and controls](docs/RUNNING.md) for all options.

The [documentation](docs/README.md) covers behavior, architecture, tests, debugging, performance, and the planned STM32 port. The offline [rule lab](rule-lab.html) lets you create and test individual rules; see its [guide](docs/RULE_LAB.md).
