# Optimization queue

## Correctness

- Preserve instruction-boundary collision stops, 50 ms recovery, HALT precedence, and private rule ownership.
- Handle bounded placement failure; check population requests under active leases/collisions.
- Review one-second accrual clamp at large divisors.
- Fix single-page capture deltas if required; define any additional dump metadata.

## Measurements

- Rebaseline unbatched, batched, saturated, one-worker, and two-worker runs after mutation changes.
- Measure per-instruction flag reads before reducing publication frequency.
- Separate scheduler scan/recovery costs from tape and occupancy costs.
- Measure controller capture/conversion, SDL uploads, dropped frames, and memory across monitor counts.
- Change bookkeeping/wakeup policy only after identifying dominant costs; preserve lease/token semantics.

## Porting

- Select board/panel; budget RAM and transfer buffers.
- Add compile-time occupancy backend; separate policy from pthread/time/entropy APIs.
- Define cross-core layout, synchronization, rule storage, boot/reset ownership.
- Bring up both workers before display transport.

[Experiments](EXPERIMENTS.md) · [STM32 integration](stm32/INTEGRATION.md) · [History](history/OPTIMIZATION_NOTES.md)
