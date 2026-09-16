# Optimization pass 1

This pass is intentionally conservative: preserve the machine semantics first, then optimize representation and hot loops.

## Memory

The previous `Ant` was 104 bytes on x86-64 in the reference build. The new `Ant` is 72 bytes: about 31% smaller. Across 32 ants, that reduces the colony object from roughly 3328 bytes to 2320 bytes before allocator/object overhead.

Changes:

* lifecycle booleans became one atomic flag word
* redundant stored ant ID was removed; array index is the ID
* unused `quantum_hint` was removed
* floating-point token fields were replaced with compact integer/fixed-point fields
* each ant owns a small LFSR state rather than participating in a shared runtime RNG
* scheduler-only token timestamp remains non-atomic

## Hot path

The instruction path previously called the monotonic clock for every instruction and performed multiple atomic floating-point loads/stores for token accounting. The new path samples time at scheduler boundaries and spends one fixed-point token per instruction.

The instruction loop also avoids the generic `world_index()` helper. Because ant coordinates are maintained in bounds, the cell index is directly `y * width + x`.

The scheduler still scans at most 32 ant contexts per dispatch. This is intentionally left as O(32): a more elaborate queue/heap would add complexity while the architectural population ceiling is only 32.

Collision detection remains O(32) by design because occupancy RAM is deliberately excluded from the machine model.

## Important semantic constraint

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
