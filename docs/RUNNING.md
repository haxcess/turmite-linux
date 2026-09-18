# Running

## Build

Dependencies: C17 compiler, make, pkg-config, pthreads, SDL2 development files. Debian/Ubuntu:

```sh
sudo apt install build-essential pkg-config libsdl2-dev
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
| `-w`, `--workers N` | 2 per universe; 1–8 |
| `-p`, `--display N` | 0; single-window display index |
| `-W`, `--windowed` | Default: one normal window |
| `-F`, `--fullscreen` | Fullscreen on selected display |
| `-A`, `--fullscreen-all` | Independent fullscreen universe per detected monitor |
| `-x`, `--width N`; `-y`, `--height N` | 1200 × 800; windowed/headless |
| `-m`, `--minutes N` | 0.4 (24 seconds); positive restart interval |
| `-u`, `--hud`; `-n`, `--no-hud` | Show/hide HUD; hidden by default |
| `-H`, `--headless` | One universe without SDL video initialization |
| `-d`, `--dump-pages N` | 127; 1, 3, 7, …, 1023 |
| `-i`, `--dump-interval N` | 1 second; positive |
| `-D`, `--dump-dir PATH` | `./turmite-dumps` |
| `-h`, `--help` | Print help |

Values above follow source defaults and validation; current help text still lists older population, quantum, and lifetime values.

Last mode (`-W`/`-F`/`-A`) and HUD flag win. `-A` ignores `-p`. Fullscreen uses monitor dimensions; application minimum is 160 × 120. Monitor discovery occurs at startup; no hotplug. Wayland controls normal-window placement.

```sh
./turmite -F -p 1 -u
./turmite --ants 4 --workers 4 --quantum 790 --min-service 1000
./turmite --headless --width 300 --height 200 --dump-pages 7
```

## Controls

Keys affect the focused universe.

| Key | Action |
| --- | --- |
| Space | Pause/resume dispatch |
| `[` / `]` | Decrease/increase quantum |
| `-` | Request population halving through token drain |
| `+` / `=` | Request doubling, capped at 32 |
| R | Clear and reseed |
| H | Toggle HUD |
| Q | Stop workers, capture final page, write dump, close window |
| Esc / window close | Close without dump |

Headless: `Q`/`q` then Enter dumps and exits. Closing one window leaves others running; last close exits. SDL application quit closes all.

Pause permits outstanding grants to finish; lifecycle and capture continue. Restart restores configured population/quantum and uses fresh entropy. `--minutes` restarts universes; it does not terminate the process.

[Behavior](SPEC.md) · [Dumps](DEBUGGING.md)
