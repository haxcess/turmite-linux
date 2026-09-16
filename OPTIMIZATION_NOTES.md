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

The second pass fixes an unintended population-loss bug: HALT rules no longer decrement the active population. The Busy Beaver rule contains a HALT transition, so the previous implementation could silently shrink the colony without a requested population reduction.

The ant context was reduced from 72 bytes to 48 bytes by moving scheduler-only timestamps and cold diagnostic counters out of the hot structure. Scheduler timestamps are stored in `Scheduler.last_token_us[]`; instruction/mutation counters are stored in `AntColony.stats[]`.

The executor now accepts a scheduler-granted instruction count, allowing the inner loop to avoid loading the token balance on every instruction. State/heading publication and instruction accounting happen once per quantum.

The token accrual math was simplified to one 64-bit division after clamping elapsed time to one second.
