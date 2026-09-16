# Optimization task queue

## Current / measured

1. **Collision lookup hot path**
   - Baseline profile walked 48-byte `Ant` structs and spent a large fraction of samples loading candidate state.
   - V4 uses a packed structure-of-arrays occupancy view: 32 atomic `(y<<16)|x` positions = 128 bytes, aligned to a 64-byte cache line boundary, plus a 32-bit enabled mask.
   - Still O(N) worst-case, but scans only enabled bits with `ctz` and touches only packed positions.
   - Re-profile before replacing the algorithm.

2. **WFQ scheduler scan / mutex**
   - Previous profile showed `scheduler_acquire()` disproportionately hot.
   - V4 instruments `empty_scans`, `idle_waits`, granted instructions, and average executed instructions/dispatch.
   - Determine whether cost is successful O(32) scanning, mutex serialization, or token starvation before redesigning.

## Next candidates

3. If packed collision scanning remains hot, evaluate a small spatial hash / bucket bitmask index. Do not add a full per-cell occupancy map unless measurements justify changing that design principle.
4. Reduce scheduler shared-state traffic / full scans while preserving WFQ semantics.
5. Separate single-worker throughput limits from two-worker coherency/lock contention.
6. Revisit world-cell atomics only after collision and scheduler costs are reduced.


### Completed in V5
- Replace O(32) collision scan with O(1) CAS occupancy index.
- Preserve authoritative ant position separately from the derived occupancy cache.
- Add displaced-loser reclamation and smoke-test occupancy consistency.

### Next
1. Profile V5 saturated and normal token-rate runs against the V4 baseline.
2. If scheduler dominates normal-rate runs, prototype minimum-service batching without changing long-term token rates.
3. Revisit world-cell atomic/interpreter costs only after the above.
