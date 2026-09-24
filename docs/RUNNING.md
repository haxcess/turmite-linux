# Running

## Build

Dependencies: C17 compiler, make, pkg-config, pthreads, SDL2 and libpng development files. Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config libsdl2-dev libpng-dev
make
./turmite
```

## Options

| Option | Default / range |
| --- | --- |
| `-s`, `--seed HEX` | Host entropy; explicit seed controls initialization |
| `-a`, `--ants N` | 3; integers 2–32 |
| `-q`, `--quantum N` | 30; 2–4096 |
| `-b`, `--min-service N` | 16; 1–4096 |
| `-v`, `--token-rate-divisor N` | 1; positive 32-bit divisor |
| `-w`, `--workers N` | 2 process-wide; 1–8 |
| `-p`, `--display N` | 0; single-window display index |
| `-W`, `--windowed` | Default: one normal window |
| `-F`, `--fullscreen` | Fullscreen on selected display |
| `-A`, `--fullscreen-all` | Independent fullscreen universe per detected monitor |
| `-x`, `--width N`; `-y`, `--height N` | 1200 × 800; windowed/headless |
| `-R`, `--random` | Random HSV hues; saturation 80%, value 90% (color 0: 20%) |
| `-r`, `--randomish` | Six hues spaced 32° across a random 160° arc; saturation 64–96%, value 90% (color 0: 20%) |
| `-g`, `--glitter N` | Startup confetti; required integer 1–10; disabled when omitted |
| `-c`, `--cell-size N` | 1; integer pixels per cell, 1–10 |
| `-m`, `--minutes N` | 0.4 (24 seconds); positive restart interval |
| `-u`, `--hud` | Show HUD; hidden by default |
| `-M`, `--collision-mutation` | Enable collision-induced rule mutation; disabled by default |
| `-H`, `--headless` | One universe without SDL video initialization |
| `-d`, `--dump-pages N` | 127; 1, 3, 7, …, 1023 |
| `-i`, `--dump-interval N` | 1 second; positive |
| `-D`, `--dump-dir PATH` | `./turmite-dumps` |
| `-h`, `--help` | Print help |

Values above follow source defaults and validation; current help text still lists older population, quantum, and lifetime values.

Last mode (`-W`/`-F`/`-A`) wins. `-A` ignores `-p`. Fullscreen uses monitor dimensions; application minimum is 160 × 120. Monitor discovery occurs at startup; no hotplug. Wayland controls normal-window placement.

```sh
./turmite -F -p 1 -u
./turmite --ants 4 --workers 4 --quantum 790 --min-service 1000
./turmite --headless --width 300 --height 200 --dump-pages 7
```

## Cell size

`--width`/`--height` remain canvas pixel dimensions; fullscreen uses monitor pixels. With `-c N`, logical dimensions are `floor(width/N) × floor(height/N)`. SDL scales that texture to fill the unchanged window using nearest-neighbor sampling. Non-divisible dimensions can produce slightly uneven cell widths; no border is left unused. Headless mode applies the same division.

```sh
./turmite --fullscreen --cell-size 4
./turmite -W --width 1200 --height 800 -c 5
```

Tape, occupancy, snapshots, ARGB frames, and dump pages shrink by approximately `N²`; SDL output resources remain window-sized. A 1920 × 1080 canvas at size 4 simulates 480 × 270 cells. Dumps record logical dimensions.

## Controls

Keys affect the focused universe.

| Key | Action |
| --- | --- |
| Space | Pause/resume dispatch |
| `[` / `]` | Decrease/increase quantum |
| `-` | Request population halving through token drain |
| `+` / `=` | Spawn one random library ant, capped at 32 |
| R | Clear and reseed |
| H | Toggle HUD |
| Q | Stop token refill, export PNG frames while remaining tokens drain, write final dump, close window |
| Esc / window close | Close without dump |

Headless: `Q`/`q` then Enter drains remaining tokens, exports PNG frames, writes the final dump, and exits. Closing one window leaves others running; last close exits. SDL application quit closes all.

Pause permits outstanding grants to finish; lifecycle and capture continue. Restart restores configured population/quantum and uses fresh entropy. `--minutes` restarts universes; it does not terminate the process.

[Behavior](SPEC.md) · [Dumps](DEBUGGING.md)

Glitter requires a density, such as `--glitter 7` or `--glitter=7`. Omit the option to disable glitter. Each density level adds one shard per 1,000 cells (rounded up); shards are 3–7 by 1–3 simulation pixels, randomly horizontal or vertical, in colors 1–5. They wrap at edges and may overlap. `--cell-size` scales them with the tape. Explicit seeds reproduce the pattern; resets regenerate it.
