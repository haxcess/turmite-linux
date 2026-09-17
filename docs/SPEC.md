# Turmite Universe — Current implementation specification

This document describes the checked-in Linux implementation. Planned STM32 behavior is identified separately. Optimization version names describe development stages, not separate supported runtime modes.

## Machine and rules

- C17 with pthread workers; Linux schedules the workers normally.
- A toroidal 2-D tape contains six logical colors, stored as independent relaxed atomic bytes.
- The application supports initial populations of 2, 4, 8, 16, or 32; the fixed context limit is 32.
- The catalogue currently contains 21 rules (including the three saved lab exports). A rule declares its active colors/states within a table supporting six colors and four states.
- Each instruction spends one token, reads the tape, selects an action, writes a color, updates state/heading, and optionally moves or halts.
- Turns can be relative or absolute. `TURN_H` holds position; it is separate from the action's `halt` flag.
- An out-of-range state/color uses a fallback that preserves color and state and moves forward. Colors unused by a particular rule remain part of the shared six-color world.
- HALT marks an ant halted and ends its grant. Recovery creates a fresh random rule and scheduling phenotype at the retained position without decrementing population. The startup catalogue contains no HALT actions; runtime mutations can introduce them.
- Tape writes and movement are separate operations; rendering does not run in the instruction path. There is no transactional turmite step or global simulation tick.

## Scheduling and tokens

The shared scheduler uses a pthread mutex and condition variable. A worker leases an eligible ant, executes outside the lock, and releases the lease with its executed instruction count. An ant cannot have two worker leases at once.

The only policy is a compact WFQ-like credit scan, not a formal virtual-finish-time implementation. Each eligible candidate gains its weight in credit; the highest score wins, with lower ant index breaking ties. Executed work is subtracted on release. Fair credit uses `double`; token balances use unsigned Q16 fixed point.

Token accrual is lazy at scheduler boundaries using monotonic microseconds. Initial and HALT-reborn phenotypes randomize:

- token rate: 50,000..2,000,000 instructions/second before global scaling;
- bucket capacity: 128..4096 whole tokens;
- weight: 1..16.

Initial and HALT-reborn ants start with full buckets. Collision mutations preserve the existing token balance, rate, capacity, weight, and fair credit; ordinary elapsed-time token accrual still applies. `--token-rate-divisor` slows accrual without changing inherited per-ant rates. The benchmark additionally exposes a rate multiplier. Accrual clamps elapsed time to one second per update.

`quantum` caps the instructions in a lease. A normal ant needs at least `min(min_service, quantum, floor(token_capacity))` whole tokens before dispatch; its grant can exceed that threshold, up to its available tokens and quantum. Draining ants bypass the batching threshold. If no work is eligible, workers use a condition-variable timed wait of approximately 1 ms.

Per-universe application defaults are 8 ants, 2 ant workers, quantum 32, minimum service 16, and divisor 1. The command line permits 1..8 workers and quantum/minimum-service values of 1..4096. The standalone benchmark has separate defaults and limits documented in [MEMORY_AND_PERF.md](MEMORY_AND_PERF.md).

## Occupancy, collisions, and mutation

The derived occupancy index stores one atomic byte per cell: 0 means empty; 1..32 identify ant index + 1. Movement conditionally releases the old cell, then claims the destination with compare-and-swap. This transfer is not one atomic transaction.

On contact, greater current token balance wins; equal health favors lower ant index. A winning challenger replaces the resident occupancy byte. The loser is marked `CLOBBERED | DISPLACED`, stops at an instruction boundary, and releases its lease. A failed movement claim ends the grant immediately; an instruction already in flight on a concurrently displaced resident may finish.

The scheduler pauses the loser for 50 ms (`COLLISION_PAUSE_US`), measured from its first idle recovery scan. It then changes exactly one field of one action in that ant's private rule. It does not reroll dimensions, position, heading, internal state, token phenotype, balance, or fair credit. The private RNG advances and diagnostic mutation count increases. A 1/16 branch toggles HALT; otherwise a mutable write-color, turn, or next-state field is selected and changed to a different valid value. The other properties retain their ordinary runtime behavior, including token refill and draining.

After mutation, `WAITING` gates execution until the ant can reclaim its published cell. If a winner remains there, the wait can exceed 50 ms; the loser is not teleported or mutated repeatedly while waiting. Clearing the old event before reclaim means a newly reported collision is not erased. Rule changes only occur when no worker leases the ant; dumps never read private rule tables.

HALT is an explicit exception to collision-only variation. It generates a new 1–4-state, 1–6-color rule and resets heading, internal state, rate, capacity, weight, bucket balance, and fair credit at the retained position. Complexity weights are 8:4:2:1 for states and 32:16:8:4:2:1 for colors. Actions randomly choose a valid write color, turn, and next state, with HALT probability 1/64 per action. If HALT and collision are both pending at the same boundary, HALT takes precedence. No random runtime rules are created at universe initialization; it selects only the immutable built-in catalogue. Cloning copies a rule unchanged into independent storage.

During a lease, position/heading/state are worker-local. Packed positions are published once per quantum as `(y << 16) | x`; they are not used for hot-path collision discovery. The live residency index is `occupancy[]`.

Focused mutation tests cover early grant termination, delayed single-field recovery, blocked residency, HALT rebirth, and clone isolation. Concurrent stress tests exercise mutations alongside eight workers, population changes, and live captures.

## Population controls

Doubling requests up to twice the current population, capped at 32. Each free destination slot clones the healthiest eligible non-leased source available at that point. A clone inherits rule, heading, state, rate, capacity, and weight, receives a new RNG stream and random empty position, and starts with a **full** token bucket. Clones created earlier in the same operation can themselves become sources.

Halving selects weak, enabled, non-leased ants that are not already draining and sets their token generation to zero. They expire when their remaining balance falls below one whole token; retirement releases occupancy and decrements the population.

These are best-effort requests against currently available ants. Busy leases can prevent reaching the requested change in one keypress, collision mutations preserve draining state, and HALT rebirth clears it. Intermediate populations need not be powers of two. Initial placement uses bounded random probing rather than an exhaustive empty-cell search; callers currently assume placement succeeds.

## Randomness

The universe generator and per-ant runtime generators use a 32-bit Galois LFSR with feedback mask `0x80200003`. Zero seeds are remapped. Linux entropy comes from `/dev/urandom`, with a process/time fallback; `--seed` supplies an explicit initial seed.

For M selected displays, initial seed i is `base_seed XOR (0x9e3779b9 × i)` in 32-bit arithmetic, with zero remapped to 1. The first/only universe keeps the supplied seed; i is the window ordinal starting at zero, not its SDL display index. Each universe owns its RNG streams, scheduler, colony, tape, and capture ring.

An explicit seed establishes initial RNG sequences, not deterministic concurrent execution. Timing affects token accrual, dispatch, and collisions. Automatic and manual restarts choose fresh entropy even if the first universe used `--seed`.

## Display and memory

SDL defaults to one 1200×800 normal window on display 0 with the HUD hidden. `--hud` (`-u`) enables the HUD; `--no-hud` (`-n`) hides it. `--fullscreen` (`-F`) fills the display selected by `--display N` (`-p N`, default 0). `--fullscreen-all` (`-A`) fills every monitor detected at startup with an independent universe, ignoring `--display`. Fullscreen worlds use each monitor's reported dimensions. The last mode flag (`-W`, `-F`, `-A`) wins; `-W` restores a single normal window on the selected display. The last HUD flag wins. Headless mode runs one 1200×800 universe by default. There is no runtime monitor hotplug handling. The application requires at least 160×120; `world_init()` permits dimensions up to 65535 per axis for packed positions, subject to allocation limits.

One cell maps to one display pixel. The render loop targets 60 FPS and does not gate worker execution. There are no ant labels; an optional HUD shows population, workers, quantum, minimum service, age, seed, collisions, and pause state.

`World` contains only `data[]`: one atomic byte per cell containing the logical index. Occupancy remains a separate core allocation. There are no ink buffers, palette fields, or render callbacks in the core.

Each graphical universe has a controller thread and its own configured ant worker pool. The controller reads a row-major snapshot into its index buffer and passes a `RenderFrame` to portable RGB conversion. Three RGB buffers carry completed frames and HUD snapshots to main, which owns all SDL calls and presents the newest available frame. Older unread frames may be dropped. Main never scans the tape or converts colors; SDL uploads and presentation still scale with window count. Reads remain independent observations while workers run, but the completed snapshot is stable for rendering. HUD values are sampled separately into plain metadata. Neither the portable renderer nor SDL accesses `World`, ants, or scheduler state.

Portable `render_argb()` maps indices through a caller-provided six-color RGB palette; Linux uses the hand-tuned Solarized-inspired palette defined in `src/renderer.c` and listed in [RENDERING.md](RENDERING.md). Logical zero maps to a dark teal background; it does not require pure-black RGB. `render_codes()` maps the same indices to caller-provided byte codes for another display backend. Invalid indices map to logical zero. Conversion functions allocate no storage and depend on no OS, hardware driver, or simulation structures.

Palette drift and historical per-write ink have been removed. A same-color rewrite has no distinct visual state. Resetting the tape is enough to reset the displayed image on its next frame; there is no presentation history to clear. Headless mode allocates neither the graphical snapshot nor RGB conversion buffers. See [RENDERING.md](RENDERING.md) for frame ownership and [MEMORY_AND_PERF.md](MEMORY_AND_PERF.md) for storage costs.

## Lifecycle and observation

Each universe has its own Linux lifecycle software timer, default five minutes. Expiry or `R` stops and joins workers, clears state, reseeds, reinitializes the scheduler, and starts another universe. The process continues until quit. A future hardware-reset lifecycle is not implemented.

Pause prevents new normal dispatches, but outstanding leases can finish. Lifecycle time, capture, and rendering continue, and elapsed-time token accrual can refill buckets on resume. Runtime quantum changes are not retained across restarts; configured startup values are reapplied.

`Q` stops the focused universe's workers, takes a final capture, writes its retained ring, and closes its window. `ESC`/window close closes only that universe without writing a dump. Other keys also affect only the focused window. The process exits when the last window closes; an SDL application-wide quit stops all universes. SDL shuts down once, after all windows are destroyed. Headless input supports `Q`/`q` followed by Enter.

Captures default to 127 pages per universe at one-second intervals. Valid capacities are 1, 3, 7, 15, 31, 63, 127, 255, 511, and 1023; indexing uses modulo capacity. Each raw page stores only the logical tape. Metadata includes age, FNV-1a hash, changed cells, global RNG state, population, quantum, minimum service, counters, and ant position/rule/token/credit snapshots.

Live captures are not globally synchronized snapshots. Published ant positions can lag current execution. Captures store logical colors suitable for the fixed-palette renderer, but omit per-ant RNG state and the global token-rate divisor, so they cannot fully reconstruct a run. With one retained page, the change count compares the newly overwritten slot with itself and is always zero. A restart discards the previous universe's capture ring and all runtime rule variants. Runtime rules use `rule_index=65535`, `rule_id=runtime`, and a descriptive label in dumps; no generated or mutated action tables are saved. The `mutations` counter counts collision edits only; instruction/mutation counters belong to the slot over the universe lifetime, including HALT rebirths.

## Remaining work and boundaries

- Port the instruction-boundary collision stop, scheduler-owned recovery timer, private rule tables, and best-effort population semantics explicitly to STM32.
- Minimum batching is capped at each ant's whole-token capacity so full buckets remain eligible even when the configured minimum exceeds capacity.
- The one-second accrual clamp can discard elapsed refill time with large slow-motion divisors, where a bucket may take longer than one second to fill.
- Rule-driven scheduling mutations, additional scheduler policies, and a gene-pool/bookmark interface are not implemented. `RuleAction` currently contains only write, turn, next-state, and halt fields.
- STM32 timing, entropy, scheduler synchronization, shared-state layout, startup, lifecycle, and display integration remain to be implemented. H745/H747 is the existing proposed target; the board and specific panel remain unselected. The display target is six-color e-ink, approximately 8×6 inches, with pixel resolution and interface still open. See [stm32/README.md](stm32/README.md).
