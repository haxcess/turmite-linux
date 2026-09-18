# STM32 scaffold

Proposed target: H745/H747, CM7 + CM4, separate FreeRTOS instances. Display target: six-color e-ink, approximately 8 × 6 inches. Board, panel, resolution, interface, and refresh policy remain unselected. No runnable firmware or panel driver.

## Files

| Path | Status |
| --- | --- |
| `stm32/include/turmite_stm32_shared.h` | Occupancy descriptor/API |
| `stm32/src/turmite_stm32_shared.c` | HAL HSEM claim/replace/release, 16 stripes, `taskYIELD()` on contention |
| `stm32/include/turmite_stm32_memory.h` | `.turmite_shared` attribute only; no linker layout/storage |
| `stm32/include/turmite_stm32_port.h` | Time, entropy, notification, wait declarations only |

Excluded from Linux build; requires external HAL/FreeRTOS headers.

## Proposed ownership

| Core | Tasks |
| --- | --- |
| CM7 | Shared initialization, scheduler/control, worker, lifecycle, display |
| CM4 | Worker; waits for CM7 readiness |

Shared SRAM: tape, occupancy, shared ant/scheduler/control fields; proposed non-cacheable CM7 MPU mapping. Private: stacks and worker-local execution. Notifications: HSEM/mailbox.

Occupancy uses HSEM 0–15, stripe `cell & 15`, barriers around one-byte operations. Diagnostic loads are unlocked. Supply zeroed storage; owners 1–32. Other shared fields and same-core task/interrupt exclusion require separate protocols.

Linux still uses direct occupancy atomics and pthread scheduling. Port work includes backend separation, clocks/entropy, shared rule tables, atomic-width audit, linker/boot/reset setup, and panel transport.

[Integration checklist](INTEGRATION.md) · [Rendering API](../RENDERING.md) · [Memory budget](../MEMORY_AND_PERF.md)
