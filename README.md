# Turmite Universe

An academic art piece exploring multithreading, evolution, and cellular automata. Concurrent turmites draw on a shared six-color canvas. The implementation combines weighted scheduling, collision-driven rule mutation, batched execution, and constant-time collision lookup. Simulation runs independently of display refresh.

## Run

Requires a C17 compiler, make, pkg-config, pthreads, and SDL2 development files.

```sh
make
./turmite
./turmite --fullscreen-all
./turmite --help
```

Default: one 1200 × 800 window on display 0, HUD hidden. **Esc** closes a window.

| Option | Function |
| --- | --- |
| `-W`, `-F`, `-A` | Windowed, selected-display fullscreen, all-display fullscreen |
| `-p N` | Select display for single-window modes |
| `-u` | Show HUD |
| `-c N`, `--cell-size N` | Pixels per cell (1–10); preserves window dimensions |
| `--ants N`, `--workers N` | Population per universe; process-wide workers |
| `--quantum N`, `--min-service N` | Execution batch limits |
| `-v N` | Token refill divisor |
| `--minutes N` | Universe restart interval |

[Options and controls](docs/RUNNING.md) · [Technical documentation](docs/README.md) · [Rule lab](rule-lab.html) ([guide](docs/RULE_LAB.md))
