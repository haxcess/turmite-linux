# STM32 integration checklist

Pending H745/H747 design; no Cube projects, linker scripts, worker tasks, or hardware validation.

## Hardware and memory

- Select part/board, shared/external RAM, panel/controller, resolution, encoding, interface, refresh timing.
- Budget tape + occupancy at two bytes/cell, private rule tables, scheduler state, stacks, and diagnostics.
- Choose panel buffers independently of Linux RGB/capture storage. `render_codes()` supplies byte mapping; packing/transport remain backend work.

## Platform boundaries

- Split token/credit policy from pthread locks, waits, clocks, and notifications.
- Add compile-time Linux CAS / STM32 HSEM occupancy backends.
- Define ownership for flags, leases, health, positions, RNG, counters, and reset state; audit atomic widths with the selected toolchain.
- Use cross-image catalogue IDs and explicit runtime-table storage. Sentinel 65535 does not identify a table. Deep-copy clone tables; publish mutations only after lease release.
- Port instruction-boundary collision stop, 50 ms recovery, retained-cell waiting, and HALT precedence from [SPEC](../SPEC.md).

## Firmware and HSEM

Create CM7/CM4 HAL/startup projects, each with FreeRTOS and one worker. CM7 owns control/lifecycle and display. Implement port-header time, entropy, notify, and wait APIs. Waits must permit token-driven progress without external notifications.

| HSEM | Proposed use |
| --- | --- |
| 0–15 | Occupancy stripes; existing helper |
| 16 | Scheduler/control lock |
| 17 | CM7 → CM4 work notification |
| 18 | CM4 → CM7 completion |
| 19 | Reset notification |
| 20–31 | Reserved |

Reconcile boot/peripheral reservations; configure clocks, interrupts, notifications. Keep tape writes outside occupancy critical sections. Define local task/interrupt exclusion; the current yielding helper is not a general scheduler or interrupt-safe lock.

## Shared SRAM and boot

Both linker scripts must reserve the same `.turmite_shared` region/layout/alignment. Supply storage; prevent CM4 startup clearing CM7-initialized data. Configure CM7 MPU before shared access/releasing CM4. Define display DMA placement/cache ownership separately.

CM7 sequence: clocks/MPU → shared initialization → seed/scheduler → signal readiness → tasks. CM4 waits for readiness. Quiesce both cores before reset/reinitialization. Hardware watchdog lifecycle remains undecided.

## Time and entropy

Seed each universe from MCU entropy; use runtime LFSRs thereafter. Both cores require comparable monotonic `uint64_t` microseconds. Extend a 32-bit timer across wrap: at 1 MHz it wraps in ~71.6 minutes. Universe resets do not imply timer resets; the host `now_us <= last_us` check is not wrap-safe for raw 32-bit time.

## Bring-up checks

Via SWD/UART, verify shared tape/catalogue, exclusive leases, occupancy, mutation/HALT recovery, population changes, token accounting, timer wrap, and coordinated boot/stop/restart. Then add independently scheduled capture/refresh with explicit DMA buffer ownership.
