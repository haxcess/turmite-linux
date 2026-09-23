# Debug dumps

Per-universe ring: initial capture, periodic captures, final capture after Q drains the remaining token budget. Default 127 pages at 1-second intervals. Capacities: 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023; modulo indexing. Restart discards the ring.

```sh
./turmite --dump-pages 31 --dump-interval 0.5 --dump-dir ./captures
# Press Q; in headless mode, Q then Enter.
python3 tools/analyze_dump.py ./captures/tape-SEED-TIMESTAMP --page 0
```

Replace the example path with the emitted directory. Default parent: `./turmite-dumps`.

| File | Contents |
| --- | --- |
| `manifest.txt` | Dimensions, seed, scheduler settings including token-rate divisor, chronological page and ant metadata |
| `pages/page-NNNN.bin` | Row-major uint8 logical indices 0–5 |
| `frames/frame-NNNNNNNN.png` | Every rendered frame during debug drain, numbered from zero; includes the final stable frame |
| `universe.png` | Final captured page using the active six-color palette; one pixel per logical cell, no HUD |
| `README.txt` | Raw format notes |

`universe.png` is a 4-bit indexed, non-interlaced PNG using the retained writer's
sRGB metadata and 300 DPI settings. Its dimensions follow the logical grid,
including `--cell-size`. The manifest also records the active palette.
Q stops token generation and resumes paused ants so workers can spend the tokens
already present. Minimum batching does not prevent spending the last whole token;
sub-token remainders cannot execute an instruction. Collision recovery continues,
and HALT rebirth preserves the remaining balance instead of filling a new bucket.
Pause, restart, and population controls are ignored during the drain.

Every controller-rendered frame is exported from the same snapshot as the display,
at the normal 30 Hz target cadence (PNG encoding can lower the achieved rate).
Headless mode uses the same snapshot/export cadence during draining. A final
stable frame is exported after all leases return; `universe.png` matches it.
A short drain may produce only an initial frame and the final frame. No artificial
worker delay is added. Normal rendering before Q has no PNG encoding overhead.

Periodic page capture and the rolling ring continue during draining. The final
page and manifest are written on completion in their existing formats and layout.
The PNG sequence uses disk space proportional to the number of exported frames,
without retaining frames in memory. Export failures produce a nonzero exit status.
Esc/window close can still interrupt a drain.

Page metadata includes age, FNV-1a hash, changed cells, global RNG, population, quantum, minimum service, counters, and ant position/rule/token/credit observations.

## Limitations

- Live tape and metadata samples are not globally synchronized; published positions can lag execution.
- Per-ant RNG states and runtime rule tables are omitted. Runtime rule identity: `rule_index=65535`, `rule_id=runtime`.
- One-page rings always report `changed_cells=0` because comparison follows overwrite. Use at least three pages for deltas.
- Repeated hashes indicate sampled tape repetition, not full-state cycles. Dumps are not replay checkpoints.
- Analyzer reports statistics, not images. Decode logical indices through the [palette](RENDERING.md).
- Capture memory is allocated even without a dump request; see [storage costs](MEMORY_AND_PERF.md).

Missing trails: see [invisible-ant diagnostics](RULE_LAB.md#investigating-invisible-ants).
