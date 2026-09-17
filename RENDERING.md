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

`src/renderer.h` and `src/renderer.c` depend only on standard C types and the shared logical color count. They do not depend on SDL, pthreads, HAL, FreeRTOS, allocation, `World`, or scheduler structures. `src/renderer_sdl.h` and `src/renderer_sdl.c` own the Linux-specific window, texture, pixel storage, and developer HUD.

The Linux host in `main.c` samples the world and diagnostic values. Cells are read independently while workers continue; the snapshot is not a globally frozen simulation instant. Once captured, its storage remains stable for the duration of rendering. HUD sampling is separate and is not a transactional match to the image.

## Frame and buffer ownership

`RenderFrame` provides `width`, `height`, and a pointer to `width × height` bytes, row-major from the upper left. Each valid byte is 0..5. Raw logical dump pages already use this representation; dimensions come from the manifest. No image loader is included.

The caller owns the snapshot storage. Converters do not allocate or retain it. The caller must supply sufficiently large, non-overlapping input/mapping/output buffers; dimensions and capacities are checked before output is written. Malformed color indices map to index 0, while invalid dimensions, null buffers, or insufficient output capacity return failure.

- `render_argb()` maps indices through six packed RGB colors to opaque `0xAARRGGBB` output.
- `render_codes()` maps indices through six caller-supplied byte codes; it does not assume a particular controller's encoding or bit packing.
- `render_frame_cells()` validates dimensions and returns the number of cells, or zero on error.

Linux uses a full-frame index snapshot and an SDL-owned RGB buffer. `renderer_render()` consumes the snapshot synchronously, so the host can reuse it on return. A future asynchronous/DMA backend must retain the input until completion or copy it into backend-owned transfer storage. The producer must not overwrite buffers still in use by the display.

The API permits a caller to convert smaller row/tile frames, but tile addressing, panel geometry, and partial-refresh support must be implemented by the chosen backend. This is not a generic e-ink driver.

## Colors and cadence

The fixed Linux palette is black, white, red, yellow, green, and blue. Those are preview RGB values, not a promise about an unselected panel's pigments or numeric codes. A backend supplies the mapping from logical indices to its supported colors. No mapping changes the tape seen by the ants.

The SDL host targets 60 FPS. An e-ink host should schedule capture and refresh according to the selected panel, independently of ant execution. A busy panel can keep showing an older frame while workers continue. Deciding when to capture, skip, queue, or refresh belongs to the host/backend, not the simulation.

The hardware target is six-color e-ink, roughly 8×6 inches. Board, pixel resolution, controller, interface, orientation, and refresh timing are not selected. A firmware backend still needs those details, controller-code packing, transport, memory/cache ownership, and error handling. There is no hardware validation in this patch.

## Reset, memory, and validation

A universe reset clears the logical tape. The next snapshot replaces the displayed image; there is no presentation history to reset. Headless mode does not allocate display buffers. Tape plus occupancy use about two bytes per cell; Linux graphical presentation adds one snapshot byte and four ARGB bytes per cell, excluding SDL-managed resources and debug capture history.

```sh
make core-test
make render-test
make sdl-test
```

The core smoke test links no rendering code. Portable-render tests exercise all six indices, custom panel mapping, stable snapshots, cleared worlds, and invalid buffers. The SDL test uses dummy video/software rendering and reads back actual output pixels, including a cleared frame. These establish the Linux/portable boundary; they do not validate STM32 memory synchronization or any physical e-ink display.
