# Optimization history

Historical stages; current contracts: [specification](../SPEC.md), [storage](../MEMORY_AND_PERF.md).

| Stage | Changes |
| --- | --- |
| First pass | Host Ant 104 → 72 bytes; atomic flag word, Q16 tokens, private RNG; clock sampled at scheduler boundaries |
| Second pass | Ant 72 → 48 bytes; cold counters/timestamps separated; granted instruction batches; HALT preserves population |
| V4 | Packed 128-byte position array; enabled-mask collision scan; benchmark rate multiplier |
| V5 | O(1) occupancy CAS replaces O(32) collision scan; adds one byte per cell |
| V6 | Position publication per grant; CAS-first destination claim; minimum-service batching |
| RGB ink | Added four-byte persistent ink plane and 240-second tint cycle; later removed |
| Rendering separation | Removed core RGB work; portable logical frames and fixed-palette conversion; core 6 → 2 bytes/cell |
| Multi-monitor | Independent controllers/workers; main-thread SDL; three ARGB buffers plus index snapshot (13 bytes/cell) |
| Runtime evolution | Collision stop, 50 ms pause, one-field mutation; weighted HALT rebirth; private rule tables and clone copies |

## V6 measurements

Local one-worker runs: 32 ants, quantum 256, 3 seconds, same container.

| Mode | Instructions | Instructions/dispatch |
| --- | ---: | ---: |
| Normal, min service 1 | ~19M | ~2.5 |
| Normal, min service 16 | ~76M | ~52 |
| Rate scale 16, min service 16 | ~94M | ~229 |

These predate current rendering and mutation behavior. Remeasure on the target host; they do not characterize current throughput or multi-monitor load.
