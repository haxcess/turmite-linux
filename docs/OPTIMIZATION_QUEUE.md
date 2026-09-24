# Optimization queue

## Correctness

- Preserve instruction-boundary collision stops, 50 ms recovery, HALT precedence, and private rule ownership.
- Spawn placement failure and active-lease behavior are covered by `make spawn-test`.
- Long-sleep accrual and extreme rate scales are covered by `make scheduler-deadline-test`.
- Fix single-page capture deltas if required; define any additional dump metadata.

## Measurements

- See [September 2026 measurements](PERFORMANCE_REVIEW.md); extend them to real multi-monitor workloads.
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
