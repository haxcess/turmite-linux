# Debugging and captures

Run commands from the repository root; source paths below are relative to that root.

## Debug dumps

Capture runs in graphical and headless modes. Each universe has its own ring, which by default retains 127 logical-world pages sampled once per second, with an initial capture and an additional final capture on debug quit. Accepted capacities are `1,3,7,15,31,63,127,255,511,1023`. Storage uses exactly that many pages and modulo indexing.

```sh
./turmite --windowed --dump-pages 31 --dump-interval 0.5 --dump-dir ./captures
```

Dumps are written under `./turmite-dumps/` by default and contain:

- `manifest.txt`: dimensions, seed, scheduler settings, chronological page hashes/change counts, and ant metadata.
- `pages/page-NNNN.bin`: one byte per logical cell, row-major, values 0..5.
- `README.txt`: raw-page format notes.

Captures contain logical indices that can be rendered through the same fixed palette; they contain no platform-specific pixels. Live samples are assembled while workers run, so a page and its metadata are not a globally frozen instant. Hashes use FNV-1a. With capacity 1, `changed_cells` is always zero because the previous page has already been overwritten. Per-ant RNG states and the global token-rate divisor are not recorded. Runtime variants are labeled `runtime` (index 65535); their rule tables are not retained.

Inspect a dump with Python 3:

```sh
python3 tools/analyze_dump.py ./turmite-dumps/tape-SEED-TIMESTAMP
python3 tools/analyze_dump.py ./turmite-dumps/tape-SEED-TIMESTAMP --page 0
```

Replace the example directory with an actual capture path. Reported repeating hashes are clues about sampled tape states, not proof that the whole simulation is cycling.

Memory grows with world resolution: the core currently uses about 2 bytes per cell, plus 1 byte per cell for the graphical snapshot and 12 bytes per cell for three prepared RGB frames, SDL-managed resources, and 1 byte per cell per retained dump page. Each monitor allocates its own world, buffers, and capture ring. At 1920×1080, the default dump pages alone require about 263 MB; at 3840×2160, about 1.05 GB. See [MEMORY_AND_PERF.md](MEMORY_AND_PERF.md) for the breakdown.

## Missing trails

A visible trail is not an ant counter. Rules can erase cells, remain local, or move without changing the background. See the [rule lab investigation](RULE_LAB.md#investigating-invisible-ants) for reproduction steps and the earlier batch-capacity bug.
