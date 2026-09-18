# Debug dumps

Per-universe ring: initial capture, periodic captures, final capture on Q. Default 127 pages at 1-second intervals. Capacities: 1, 3, 7, 15, 31, 63, 127, 255, 511, 1023; modulo indexing. Restart discards the ring.

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
| `README.txt` | Raw format notes |

Page metadata includes age, FNV-1a hash, changed cells, global RNG, population, quantum, minimum service, counters, and ant position/rule/token/credit observations.

## Limitations

- Live tape and metadata samples are not globally synchronized; published positions can lag execution.
- Per-ant RNG states and runtime rule tables are omitted. Runtime rule identity: `rule_index=65535`, `rule_id=runtime`.
- One-page rings always report `changed_cells=0` because comparison follows overwrite. Use at least three pages for deltas.
- Repeated hashes indicate sampled tape repetition, not full-state cycles. Dumps are not replay checkpoints.
- Analyzer reports statistics, not images. Decode logical indices through the [palette](RENDERING.md).
- Capture memory is allocated even without a dump request; see [storage costs](MEMORY_AND_PERF.md).

Missing trails: see [invisible-ant diagnostics](RULE_LAB.md#investigating-invisible-ants).
