# Rendering boundary

The simulation stores six logical color indices. Rendering observes those indices and owns every physical color mapping and display buffer. The current choice is a fixed six-color palette: there is no palette drift, per-write ink history, or rendering callback in the instruction path.

## Data flow

```text
ant instruction → atomic logical tape
                          ↓ host samples cells at display cadence
                  stable RenderFrame (indices 0..5)
                          ↓
         portable conversion with backend-supplied color mapping
                 ↙                              ↘
       ARGB8888 for SDL                 panel byte codes
       SDL texture/present             packing/transfer/refresh
       implemented                     hardware backend still to do
```

`src/renderer.h` and `src/renderer.c` depend only on standard C types and the shared logical color count. They do not depend on SDL, pthreads, HAL, FreeRTOS, allocation, `World`, or scheduler structures. `src/renderer_sdl.h` and `src/renderer_sdl.c` own the Linux-specific window, texture, and developer HUD. The application owns the prepared pixel buffers.

The Linux host in `main.c` samples the world and diagnostic values. Cells are read independently while workers continue; the snapshot is not a globally frozen simulation instant. Once captured, its storage remains stable for the duration of rendering. HUD sampling is separate and is not a transactional match to the image.

## Frame and buffer ownership

`RenderFrame` provides `width`, `height`, and a pointer to `width × height` bytes, row-major from the upper left. Each valid byte is 0..5. Raw logical dump pages already use this representation; dimensions come from the manifest. No image loader is included.

The caller owns the snapshot storage. Converters do not allocate or retain it. The caller must supply sufficiently large, non-overlapping input/mapping/output buffers; dimensions and capacities are checked before output is written. Malformed color indices map to index 0, while invalid dimensions, null buffers, or insufficient output capacity return failure.

- `render_argb()` maps indices through six packed RGB colors to opaque `0xAARRGGBB` output.
- `render_codes()` maps indices through six caller-supplied byte codes; it does not assume a particular controller's encoding or bit packing.
- `render_frame_cells()` validates dimensions and returns the number of cells, or zero on error.

Each Linux universe owns one full-frame index snapshot and three ARGB buffers. Its controller captures and converts into a buffer that is neither published nor held by the main thread. A short mutex-protected handoff publishes the completed pixels and HUD together. Main takes the newest frame and calls `renderer_present()`; the producer may replace an unread frame but cannot overwrite the frame held for presentation. No scan, conversion, or SDL upload holds the handoff mutex.

The synchronous `renderer_render()` convenience API remains available; it lazily allocates its own conversion buffer and consumes a `RenderFrame` before returning. The threaded application uses `renderer_present()` directly and does not allocate that extra buffer. A future asynchronous/DMA backend must retain the input until completion or copy it into backend-owned transfer storage. The producer must not overwrite buffers still in use by the display.

The API permits a caller to convert smaller row/tile frames, but tile addressing, panel geometry, and partial-refresh support must be implemented by the chosen backend. This is not a generic e-ink driver.

## Colors and cadence

The fixed Linux palette uses the hand-tuned Solarized-inspired values in `src/renderer.c`. These values are preserved by the multi-monitor changes.

| Logical index | Preview color | RGB |
| --- | --- | --- |
| 0 | Dark teal background | `#073642` |
| 1 | Slate gray | `#586E75` |
| 2 | Red | `#DC322F` |
| 3 | Yellow ochre | `#B58900` |
| 4 | Green | `#859900` |
| 5 | Blue | `#288BD2` |

This changes only color mapping: there is no blur, texture, or change to the logical world. Run `make` and restart `./turmite` after changing `src/renderer.c`; the separate `turmite.exe` is not the Makefile's output.

Those are preview RGB values, not a promise about an unselected panel's pigments or numeric codes. A backend supplies the mapping from logical indices to its supported colors. No mapping changes the tape seen by the ants.

Each SDL universe controller targets 60 frames per second; a busy UI consumes the newest completed frame and skips older unread frames. An e-ink host should schedule capture and refresh according to the selected panel, independently of ant execution. A busy panel can keep showing an older frame while workers continue. Deciding when to capture, skip, queue, or refresh belongs to the host/backend, not the simulation.

The hardware target is six-color e-ink, roughly 8×6 inches. Board, pixel resolution, controller, interface, orientation, and refresh timing are not selected. A firmware backend still needs those details, controller-code packing, transport, memory/cache ownership, and error handling. There is no hardware validation in this patch.

## Reset, memory, and validation

Linux defaults to one normal window on display 0 with its HUD hidden. `--hud` enables the HUD. `--fullscreen` fills the display selected by `--display N` (default 0); `--fullscreen-all` opens an independent fullscreen universe on every monitor detected at startup. `--windowed` restores single-window mode. Each universe has its own controller and ant workers; headless mode retains a single universe. Runtime monitor hotplug is not implemented.

SDL events, window/texture lifetime, uploads, and presentation stay on main, as required by [SDL_RenderPresent](https://wiki.libsdl.org/SDL2/SDL_RenderPresent) and [SDL_PollEvent](https://wiki.libsdl.org/SDL2/SDL_PollEvent). The controller does CPU frame preparation, dump capture, and lifecycle work. Adding monitors therefore adds threads, while SDL presentation remains serialized on main. Fullscreen windows request that SDL keep them visible on focus loss. Closing one window releases only its universe; `renderer_shutdown()` runs after all windows are destroyed.

A universe reset clears only that universe's logical tape. The next snapshot replaces the displayed image; there is no presentation history to reset. Headless mode does not allocate display buffers. Tape plus occupancy use about two bytes per cell; Linux graphical presentation adds one snapshot byte and twelve ARGB bytes per cell per universe, excluding SDL-managed resources and debug capture history.

```sh
make core-test
make render-test
make sdl-test
make multi-window-test
```

The core smoke test links no rendering code. Portable-render tests exercise all six indices, custom panel mapping, stable snapshots, cleared worlds, and invalid buffers. The SDL test uses dummy video/software rendering and reads back actual output pixels, including a cleared frame. The multi-window tests exercise concurrent frame handoff, focused controls, restart, peer survival after close, automatic monitor enumeration, and the single-display override. Enumeration is simulated over dummy video, so physical display placement and compositor behavior still need a real desktop check. These establish the Linux/portable boundary; they do not validate STM32 memory synchronization or any physical e-ink display.

## Fullscreen monitor placement

Fullscreen windows use `SDL_WINDOWPOS_CENTERED_DISPLAY` for both coordinates to explicitly select their monitor. An undefined position can delegate output selection to the Wayland compositor, including when using SDL2 through sdl2-compat, causing multiple windows to open on the same screen. See [SDL's Wayland fullscreen implementation](https://github.com/libsdl-org/SDL/blob/main/src/video/wayland/SDL_waylandwindow.c).

This targets fullscreen output selection. Ordinary `--windowed` placement remains subject to compositor policy; Wayland does not generally allow applications to position normal top-level windows. Use `--fullscreen-all` to fill all monitors, or `--fullscreen` for the selected monitor.
