# Running Turmite Universe

Run commands from the repository root; source paths below are relative to that root.

## Build and run

Dependencies include a C17 compiler, make, pkg-config, pthreads, and SDL2 development files. On Debian/Ubuntu-style systems:

```sh
sudo apt install build-essential pkg-config libsdl2-dev
make
./turmite
```

By default, Linux opens one normal window on display 0 with the HUD hidden. Use `-u`/`--hud` to show it, `-F`/`--fullscreen` for fullscreen on the selected display, or `-A`/`--fullscreen-all` for independent fullscreen universes on every monitor detected at startup. `-p N`/`--display N` selects the monitor for single-window modes (default 0). Monitor hotplug is not handled during a run.

Windowed and headless modes run one universe, default to 1200×800, and accept explicit dimensions. Fullscreen uses each selected monitor's reported dimensions. The last mode flag (`-W`, `-F`, or `-A`) wins; `--display` does not restrict `--fullscreen-all`. The last HUD flag (`-u` or `-n`) wins:

```sh
./turmite --hud --width 1200 --height 800
./turmite --fullscreen
./turmite --fullscreen-all
./turmite --display 1 --ants 16 --quantum 64 --seed 0x12345678
./turmite --windowed --token-rate-divisor 100 --min-service 32
./turmite --headless --width 300 --height 200 --dump-pages 7
./turmite --help
```

Each universe defaults to 8 ants, 2 ant workers, quantum 32, minimum service 16, and token-rate divisor 1. It accepts 1..8 workers and quantum/minimum-service values of 1..4096. A minimum service of 1 reproduces unbatched scheduling. `--minutes` changes the restart interval; it does not make the process exit after that duration.

## Runtime controls

These controls apply only to the focused window and its universe:

| Key | Action |
| --- | --- |
| `SPACE` | Pause/resume dispatch |
| `[` / `]` | Decrease/increase quantum |
| `-` | Request halving through token-drain retirement |
| `+` or `=` | Request doubling, up to 32 ants |
| `R` | Start a fresh universe with a new seed |
| `H` | Toggle the developer HUD |
| `Q` | Stop this universe, capture a final page, write its dump, and close its window |
| `ESC` | Close this window without a dump |

Closing a window leaves the other universes running; the process exits after the last window closes. An SDL application-wide quit stops all universes. Use `--hud` to start with the HUD; `--no-hud` explicitly restores the default hidden state. Headless input supports only `Q`/`q` followed by Enter for dump-and-quit. Pause does not freeze the lifecycle timer or capture schedule, and an already leased quantum can finish. Restart restores the original configured quantum and population.

## Command-line reference

| Option | Meaning / default |
| --- | --- |
| `--seed HEX` | Initial seed; concurrent timing can still change the result |
| `--ants N` | Initial population: 2, 4, 8, 16, or 32; default 8 |
| `--workers N` | Ant workers per universe: 1–8; default 2 |
| `--quantum N` | Maximum grant: 1–4096; default 32 |
| `--min-service N` | Minimum batch target: 1–4096; default 16 |
| `-v N`, `--token-rate-divisor N` | Token refill divisor; default 1 |
| `-p N`, `--display N` | Single-window display index; default 0 |
| `-W`, `--windowed` | Normal single window; default mode |
| `-F`, `--fullscreen` | Fullscreen on the selected display |
| `-A`, `--fullscreen-all` | Independent fullscreen universe on every detected monitor |
| `--width N`, `--height N` | Windowed/headless dimensions; default 1200 × 800 |
| `--minutes N` | Universe restart interval; default 5 minutes |
| `-u`, `--hud` | Show HUD |
| `-n`, `--no-hud` | Hide HUD; default |
| `--headless` | Run one universe without initializing SDL video |
| `--dump-pages N` | Capture-ring capacity; default 127 |
| `--dump-interval N` | Seconds between captures; default 1 |
| `--dump-dir PATH` | Dump output directory; default `./turmite-dumps` |
| `--help` | Show command-line help |

On Wayland, the compositor controls normal-window placement. Fullscreen requests explicitly select a monitor. See [rendering](RENDERING.md) for monitor handling and [debugging](DEBUGGING.md) for capture options and memory costs.
