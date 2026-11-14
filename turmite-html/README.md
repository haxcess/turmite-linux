# Turmite Universe · HTML

Open **index.html** in a modern browser. No build, server, dependencies, or network connection is needed; keep the five app files together. This folder can be copied independently of the native project.

The presentation borrows the black canvas, CRT vignette, scanlines, terminal typography, and hue families from `../packetLabyrinth.html`: a random base hue, five offsets in the range −45…+49°, and an accent at +45…+139°. Read heads use 95% saturation / 70% lightness. Tape colors use the reference's subdued 38% saturation, with different brightnesses to distinguish the six symbols. Color zero is almost black. The UI follows the base hue. **C** recolors the existing universe without changing its state; refreshing also picks a fresh theme.

The controls panel fades after 12 seconds; **?** or the small controls button brings it back. Its buttons also work on touchscreens.

| Key | Action |
| --- | --- |
| Space | Pause / resume |
| R | Clear and reseed, keeping population and mutation setting |
| C | Refresh color theme |
| [ / ] | Halve / double target simulation speed |
| − / + | Remove / add one turmite (1–32) |
| M | Toggle collision mutation (off initially) |
| H | Toggle statistics |
| F | Toggle fullscreen when supported |
| ? / Escape | Show or hide controls / hide controls |
| Click / tap canvas | Plant a turmite near that location |

## Browser adaptation

- Starts with eight random catalog turmites on a wrapping, shared six-symbol tape. `rules.js` is a standalone snapshot of `../tools/rule-catalog.js`; refresh it by copying that generated file after native rule updates.
- Preserves the native read → write → turn/state → move interpreter, per-ant color offsets, clamping for rules with fewer colors, all nine turn codes, and HALT rebirth. Each turmite owns its rule copy. Rebirth generates a rule with up to four states and six colors, favoring simpler machines.
- JavaScript runs sequential weighted batches instead of native worker threads and token buckets. Weights are 1–4. On collision, the resident keeps its position; the moving ant stays on its previous cell and skips two scheduling visits. Its write and state transition have already occurred. With mutation enabled, exactly one rule field changes. This is a browser adaptation, not a timing-identical native port.
- The tape starts at roughly one cell per two CSS pixels, capped at 524,288 cells. Byte arrays store symbols, occupancy, and dirty flags; a bounded dirty-index array avoids allocating per instruction. Only changed cells need RGB conversion. Canvas displays a cached image with glowing read heads.
- Simulation runs in chunks of at most 256 scheduling slots with a five-millisecond budget per animation frame. Target speed starts at 120k slots/s and caps at 1.92M; actual instruction throughput depends on the device and collisions. Late frames drop excess work rather than accumulating a backlog. Presentation is capped at four million pixels and 2× device scale.
- Hidden tabs do no simulation work and never catch up on return. Reduced-motion users start paused. Resizing scales the existing universe; **R** creates a grid fitted to the new viewport.
- No automatic lifetime reset, native dump exporter, or multimonitor/worker controls. Use **R** whenever you want a fresh universe.

## Checks

`tests.js` provides DOM-independent simulation regression checks. Load `rules.js`, `universe.js`, and `tests.js` in a JavaScript runtime, then call `runTurmiteTests()`. It throws on failure and returns the number of checks on success.
