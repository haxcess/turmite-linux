# STM32 port scaffold

This directory contains an initial design and occupancy helper for a proposed STM32H745/H747 dual-core target (CM7 + CM4). The Linux application remains the working reference. No board or specific panel has been selected, and there is no runnable firmware build in this repository.

The display target is six-color e-ink, roughly 8×6 inches. This physical size does not specify pixel resolution. The board, panel/controller, interface, and refresh policy remain open. Rendering now uses a fixed palette; historical RGB ink and palette drift have been removed.

## What exists

| File | Current role |
| --- | --- |
| `include/turmite_stm32_shared.h` | Descriptor and operations for a shared occupancy byte array |
| `src/turmite_stm32_shared.c` | HAL HSEM claim/replace/release implementation using 16 stripes and FreeRTOS `taskYIELD()` on contention |
| `include/turmite_stm32_memory.h` | GNU `.turmite_shared` section/alignment attribute; no storage or linker map is supplied |
| `include/turmite_stm32_port.h` | Declarations for microsecond time, entropy, peer notification, and work waiting; no implementations yet |
| `INTEGRATION.md` | Proposed bring-up sequence and unresolved integration requirements |

The Linux Makefile does not compile these sources. The scaffold requires external HAL and FreeRTOS headers. `src/ant.c` still accesses Linux atomic occupancy directly, and `src/scheduler.c` still combines scheduling policy with pthread synchronization.

## Proposed core split

CM7 would initialize shared state, own global scheduler/control policy, run one worker, manage the five-minute lifecycle, and own display refresh. CM4 would run the second worker after CM7 signals that initialization is complete. Their different execution speeds would remain part of the artwork's physical environment.

The proposal uses separate FreeRTOS instances on the two cores. Planned tasks are a worker and control task on CM7, a worker on CM4, and a later CM7 display task. Cross-core notifications would use HSEM events or a small shared mailbox; OpenAMP is not part of the initial design. None of these tasks or notification protocols is implemented here.

## Shared memory and occupancy

World cells, occupancy, and the cross-core parts of ant/scheduler state need a shared SRAM layout agreed by both images. The current plan uses a CM7 MPU region configured as non-cacheable. Stacks and worker-local execution state should remain private where possible. A section attribute alone does not establish placement, initialize storage, or make objects safe across cores.

The occupancy helper reserves HSEM channels **0..15**. A cell maps to `cell & 15`; claim, replace, and conditional release protect one occupancy byte with that stripe. Memory barriers surround the critical section. Diagnostic loads do not take a semaphore. Storage must be supplied and zeroed before use, and owner IDs are intended to be 1..32.

This helper only protects occupancy operations. Ant flags, token health, lease publication, counters, and scheduler/control state still need a cross-core protocol. Same-core task/interrupt access also needs a defined locking policy. The plan keeps logical tape writes independent rather than serializing entire turmite transitions.

## Portable concepts and required changes

The rule catalogue/interpreter, LFSR algorithm, Q16 tokens, WFQ-like credit policy, batching, and population concepts are candidates for sharing. Their present source files are not all platform independent:

- Replace direct occupancy atomics with a compile-time backend interface.
- Split scheduling policy from pthread locks, condition variables, and POSIX time.
- Supply MCU time and entropy providers instead of host calls.
- Define shared layout without assuming each image's static rule pointers identify the same data; stable rule indices are already available.
- Review all atomic operations, especially read-modify-write fields and 64-bit diagnostics, for the selected cross-core protocol and toolchain.
- Provide explicit storage placement/allocation and boot/reset ownership.
- Connect the existing portable six-color frame/conversion interface to a driver for the chosen panel, including packing, transfer ownership, and refresh scheduling.

The core now allocates roughly two bytes per cell for tape and occupancy. Rendering owns separate buffers; no RGB allocation is required by the core. The portable `render_codes()` helper can map a captured frame directly to panel-specific byte codes, without RGB. It does not implement transport or refresh. See [../RENDERING.md](../RENDERING.md). The main Linux application additionally retains a large capture ring. Neither should be copied into firmware without a memory budget. See [../MEMORY_AND_PERF.md](../MEMORY_AND_PERF.md).

Before preserving collision behavior in a shared interpreter, resolve the current Linux executor's continuation after failed movement claims, documented in [../SPEC.md](../SPEC.md). A passed host smoke test does not validate the HSEM implementation.

## Next milestone

Choose the board/display requirements, establish memory and shared-state boundaries, then integrate the portable core and occupancy backend. Bring up both workers, counters, and lifecycle through SWD/UART before adding independent display refresh. [INTEGRATION.md](INTEGRATION.md) is a proposed checklist, not a record of completed hardware work.
