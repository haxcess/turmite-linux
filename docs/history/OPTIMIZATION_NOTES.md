# Optimization history

These notes record earlier passes, their design assumptions, and local measurements. They are historical: later passes supersede earlier statements about collision scans, occupancy RAM, and ant sizes. Current behavior and storage are described in [SPEC.md](../SPEC.md) and [MEMORY_AND_PERF.md](../MEMORY_AND_PERF.md); outstanding work is in [OPTIMIZATION_QUEUE.md](../OPTIMIZATION_QUEUE.md).

## First pass

This pass is intentionally conservative: preserve the machine semantics first, then optimize representation and hot loops.

### Memory

The previous `Ant` was 104 bytes on x86-64 in the reference build. The new `Ant` is 72 bytes: about 31% smaller. Across 32 ants, that reduces the colony object from roughly 3328 bytes to 2320 bytes before allocator/object overhead.

Changes:

* lifecycle booleans became one atomic flag word
* redundant stored ant ID was removed; array index is the ID
* unused `quantum_hint` was removed
* floating-point token fields were replaced with compact integer/fixed-point fields
* each ant owns a small LFSR state rather than participating in a shared runtime RNG
* scheduler-only token timestamp remains non-atomic

### Hot path

The instruction path previously called the monotonic clock for every instruction and performed multiple atomic floating-point loads/stores for token accounting. The new path samples time at scheduler boundaries and spends one fixed-point token per instruction.

The instruction loop also avoids the generic `world_index()` helper. Because ant coordinates are maintained in bounds, the cell index is directly `y * width + x`.

The scheduler still scans at most 32 ant contexts per dispatch. This is intentionally left as O(32): a more elaborate queue/heap would add complexity while the architectural population ceiling is only 32.

At this stage, collision detection remained O(32) and occupancy RAM was excluded. V5 later replaced that design with a per-cell occupancy index.

### Semantic constraint retained today

Do not replace the world array with a packed 3-bit representation merely for memory savings. Six colors fit in three bits, but packed updates would turn one-cell writes into read-modify-write operations and would interfere with the intended atomic last-write-wins behavior.


## Second pass

The second pass fixes an unintended population-loss bug: HALT rules no longer decrement the active population. Rules containing HALT transitions are handled as self-reincarnation rather than population death.

The ant context was reduced from 72 bytes to 48 bytes by moving scheduler-only timestamps and cold diagnostic counters out of the hot structure. Scheduler timestamps are stored in `Scheduler.last_token_us[]`; instruction/mutation counters are stored in `AntColony.stats[]`.

The executor now accepts a scheduler-granted instruction count, allowing the inner loop to avoid loading the token balance on every instruction. State/heading publication and instruction accounting happen once per quantum.

The token accrual math was simplified to one 64-bit division after clamping elapsed time to one second.

## V4: packed collision view and scheduler starvation instrumentation

The baseline perf profile showed collision detection walking full `Ant` structures. V4 removes x/y from `Ant` and publishes each read head into a 32-entry atomic packed-position array. Each entry is `(y << 16) | x`, so the full collision coordinate set is 128 bytes. An enabled bitmask skips inactive ants using `ctz` iteration.

A profile-only scheduler token-rate multiplier was added to distinguish token fragmentation from scheduler implementation cost. It applies during token accrual, so clobber/mutation cannot silently reset the experiment.

Local results (environment-dependent) showed normal rates producing only a few instructions per successful dispatch, with zero empty scans. At 16x token rate, grants approached the requested 256-instruction quantum. This means the next scheduler optimization should consider batching/minimum service quanta or another WFQ-friendly mechanism rather than assuming the 1 ms timed wait is the dominant cost.


## V5: O(1) read-head occupancy index

The O(32) packed-position collision scan has been replaced by a world-sized, derived occupancy index. Each entry is one atomic byte: 0 = empty, 1..32 = ant id + 1. Ant positions remain authoritative; occupancy is only an acceleration structure.

Moves clear the source with a conditional CAS and claim the destination with CAS. Contention directly identifies the resident ant in O(1), after which token health decides the winner. The loser is marked CLOBBERED|DISPLACED and cannot reincarnate until it atomically reclaims its recorded position after the winner leaves. This preserves the read-head exclusivity/escape-time model without scanning the colony.

For a 300x200 world the occupancy index costs 60,000 bytes. The six-color tape remains independent relaxed atomic memory.

## V6: quantum-boundary position publication + dispatch batching

V6 continues the V5 O(1) occupancy-index work:

- `occupancy[]` is the authoritative cross-thread read-head residency index while an ant is leased.
- `positions[32]` is now a cold published snapshot. A worker writes its packed `(x,y)` once at the end of a dispatch instead of once per move.
- The normal destination-empty path uses one CAS with `expected=0`; on failure the compare-exchange result also supplies the resident ant ID.
- The old normal-path validation of an occupancy entry against `flags` + `positions[]` is removed.
- WFQ gains `min_service`: an ant normally waits until it has accumulated a useful batch of whole tokens. This does not change token generation rate; it changes burst size. Draining ants bypass the threshold so population halving still completes.

Local one-worker measurements (same container, 32 ants, quantum 256, 3 seconds) showed:

- unbatched normal (`min_service=1`): about 19M instructions, ~2.5 instructions/dispatch
- batched normal (`min_service=16`): about 76M instructions, ~52 instructions/dispatch
- saturated (`rate_scale=16`, `min_service=16`): about 94M instructions, ~229 instructions/dispatch

These are historical directional measurements, not results for the current checkout. Re-run the benchmark and `perf` targets on the target host before using them to justify further changes.

## Later presentation and control changes

The later Linux presentation pass introduced one world cell per display pixel and defaults to desktop fullscreen. Its windowed/headless default is 1200×800; the standalone benchmark remains 300×200.

That pass added a persistent atomic 32-bit RGB ink plane to `World`, written alongside the logical tape even in headless mode. SDL changes the palette for future writes on a 240-second tint cycle while previously deposited ink remains unchanged. Logical color zero stays black. This added four bytes per cell to core storage and additional work to every tape write; it is not included in the interpretation of the earlier throughput figures above.

The application defaults to minimum service 16 and supports `--token-rate-divisor` for slow accrual without rewriting per-ant phenotypes. Current clones start with full token buckets. Dumps continued to capture logical colors only, so they did not reproduce that historical RGB presentation.

The intended collision-displacement model described in the historical notes is not fully enforced by the current executor: it continues a grant after a failed movement claim. See the current specification before treating the older semantic claims as verified guarantees.

The `stm32/` directory adds an unintegrated 16-stripe HSEM occupancy helper and proposed dual-core integration notes. It does not yet provide runnable firmware or an embedded display backend.

## Rendering separation and fixed six-color output

The subsequent rendering refactor removes the ink plane and palette from `World` and removes RGB work from ant instructions. The selected behavior is a fixed six-color palette, so palette drift and per-write RGB history are removed rather than approximated at frame boundaries.

`renderer.h` / `renderer.c` now define a platform-independent, caller-owned frame of logical indices and allocation-free RGB or display-code conversion. `renderer_sdl.h` / `renderer_sdl.c` contain the SDL adapter and HUD. The Linux host samples world/HUD state before presenting; renderers do not access simulation objects. Core storage drops from about six to two bytes per cell. Graphical snapshots add one byte per cell outside the core, and SDL retains its four-byte pixel buffer.

The display target is now six-color e-ink at roughly 8×6 inches. Hardware and resolution remain unselected; the code-map interface is not a panel driver. Earlier performance measurements above have not been re-established for this refactor.

## Independent Linux universes per monitor

The multi-monitor implementation supports one universe per monitor detected at startup. Current defaults are a single normal window on display 0 with its HUD hidden; `--fullscreen-all` opts into all-monitor operation and `--fullscreen` fills only the selected display. Each universe has its own ant workers and a controller for lifecycle, captures, and CPU frame conversion. Main owns SDL events and presentation. Three prepared frames per universe permit latest-frame handoff without blocking the producer on uploads; obsolete unread frames are dropped. The palette and simulation instruction path are unchanged.

Graphical presentation storage is now 13 bytes per cell per universe (one index snapshot plus three ARGB frames), excluding SDL allocations. Worker counts and dump rings are also per universe. This is an orchestration change, not a measured throughput improvement; the older single-universe benchmark results do not characterize multi-monitor load.


## Collision rule edits and HALT rebirth

Collision recovery now stops the losing grant, waits 50 ms, edits one action field, and waits for retained-cell residency. It preserves heading/state and scheduling phenotype instead of replacing them. HALT is separate and generates a fresh rule with smaller complexities favored. Each colony owns 32 private rule slots; cloning variants deep-copies their tables. Startup remains catalogue-only, including three lab exports. Runtime tables are deliberately omitted from debug dumps.

Instruction loops now read collision flags at instruction boundaries, and changed rule behavior alters the workload. Earlier throughput figures are not measurements of this lifecycle. Focused and concurrent mutation tests should precede any optimization of the new stop/recovery path.
