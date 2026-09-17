# Optimization and porting queue

This is the current queue. [OPTIMIZATION_NOTES.md](OPTIMIZATION_NOTES.md) preserves the historical V4/V5/V6 work and its measurements.

## Implemented

- Q16 token balances and per-ant runtime RNG streams.
- Scheduler-granted instruction batches and worker-local execution state.
- O(1) collision discovery with one atomic occupancy byte per world cell (V5).
- Position publication once per quantum rather than once per move (V6).
- CAS-first empty-destination claim and resident ID retrieval on contention.
- Configurable minimum-service batching and a universe-wide token-rate divisor.
- Benchmark counters for grant size, completed work, empty scans, and idle waits.
- Rendering separated from the simulation: portable six-color snapshots/conversion, fixed-palette SDL output, no RGB ink writes or storage in the core.

- Opt-in `--fullscreen-all` independent Linux universes per detected monitor, each with its own controller and ant workers; CPU frame preparation is handed to main through three buffers for SDL presentation.

## Correctness gates before further optimization

1. Check collision-loser execution: the instruction loop currently ignores a failed movement claim and continues the grant without rechecking clobbered status. Establish the intended behavior and cover it with focused tests.
2. Check best-effort population controls under leases/collisions, and handle random-placement failure before relying on exact population counts.
3. Check elapsed-time accrual with large slow-motion divisors. Minimum batching is now clamped to each ant's capacity and covered by a dispatch regression test.
4. Correct single-page dump change counts if those captures need useful deltas; decide which additional metadata is needed for diagnostics.

The existing smoke test validates final occupancy and unchanged population during a short two-worker run. It does not settle these instruction-level, lifecycle, or embedded synchronization questions.

## STM32 preparation

1. Select the board and display, then budget tape, occupancy, presentation buffers, stacks, and shared state. H745/H747 dual-core is the existing design proposal. The display target is six-color e-ink, roughly 8×6 inches, with controller and resolution unselected.
2. Put occupancy operations behind a compile-time platform interface, retaining Linux CAS and connecting the existing 16-stripe HSEM scaffold.
3. Separate WFQ/token policy from pthread locking, waits, clocks, and notifications.
4. Integrate the portable rendering interface with the selected panel backend: code mapping, packing, refresh timing, and transfer-buffer ownership. Presentation has already been separated from the core.
5. Define cross-core state layout and ownership, including rule references, flags, tokens, 64-bit counters, memory placement, and startup. Implement time/entropy providers and firmware builds.
6. Bring up workers and lifecycle without the display, then add independent display refresh.

See [stm32/INTEGRATION.md](stm32/INTEGRATION.md) for the proposed integration sequence. The current HSEM helper is not yet a complete synchronization backend.

## Profiling gates

1. Rerun unbatched, batched, and saturated benchmarks after removing RGB work from ant instructions. Historical results describe different presentation costs.
2. Compare one-worker throughput with two-worker contention before changing shared-state traffic.
3. If scheduler scans or displaced-ant reclamation dominate, evaluate targeted bookkeeping or wakeup changes while retaining token/lease semantics.
4. Measure tape/occupancy costs in workers and snapshot/conversion costs in rendering separately before optimizing the interpreter. For multi-monitor runs, also measure aggregate controller load, SDL upload/presentation time, dropped frames, and memory use against `--display N` single-window runs.

Do not treat smaller host structures or higher saturated throughput as evidence that the firmware port is correct or that the normal visual workload improves.
