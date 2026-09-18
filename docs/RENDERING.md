# Rendering

```text
atomic logical tape → host snapshot → RenderFrame
                                     ├─ render_argb → SDL
                                     └─ render_codes → panel backend (pending)
```

Core stores logical indices only. Fixed palette; no ink history or palette drift. Same-color rewrites have no distinct visual state.

## API

`src/renderer.{h,c}`: allocation-free standard C; no simulation, SDL, or RTOS dependencies.

`RenderFrame`: width, height, caller-owned row-major bytes from the upper left; valid indices 0–5. Raw dump pages share this layout.

| Function | Result |
| --- | --- |
| `render_frame_cells()` | Cell count; zero for invalid frame |
| `render_argb()` | Six RGB mappings → opaque `0xAARRGGBB` pixels |
| `render_codes()` | Six byte mappings → panel codes; no bit packing |

Converters retain no buffers. Caller supplies stable, sufficiently sized, non-overlapping input/mapping/output storage. Invalid indices map to 0. Null buffers, invalid dimensions, or insufficient capacity fail before writing output.

## Linux buffers

Each controller targets 30 Hz, independently of workers. It samples one index frame, converts into one of three ARGB buffers, and publishes pixels plus HUD metadata under a short handoff mutex. Main presents the newest frame. Unread frames may be replaced; a frame held for presentation cannot be overwritten. Sampling, conversion, and uploads occur outside the handoff lock.

Live sampling is non-transactional; HUD and image are sampled separately. `renderer_present()` consumes prepared pixels. The synchronous `renderer_render()` adapter instead allocates a conversion buffer lazily; the application does not use it. Headless mode has no display buffers.

SDL events, uploads, window lifetime, and presentation stay on main. Fullscreen placement uses `SDL_WINDOWPOS_CENTERED_DISPLAY` for both coordinates; normal Wayland placement is compositor-controlled. Fullscreen requests visibility on focus loss. SDL shuts down after all windows close.

## Palette

`RENDER_BASE_PALETTE` in `src/renderer.c`:

| Index | RGB |
| --- | --- |
| 0 | `#073642` |
| 1 | `#586E75` |
| 2 | `#DC322F` |
| 3 | `#B58900` |
| 4 | `#859900` |
| 5 | `#288BD2` |

Rebuild and restart `./turmite` after palette edits. `turmite.exe` is a separate binary. Reset clears the tape; the next frame replaces the image.

## Panel backend requirements

Supply color mapping, geometry, packing, transport, refresh scheduling, and error handling. Tile addressing and partial refresh are backend-specific. DMA/asynchronous transfers must retain input until completion or copy it. Capture and refresh cadence remain independent of simulation.

[Tests](DEVELOPMENT.md) · [Memory](MEMORY_AND_PERF.md) · [STM32](stm32/README.md)
