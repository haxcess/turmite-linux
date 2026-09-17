# Proposed STM32H745/H747 integration

This checklist describes work still to do for the existing dual-core proposal. There is no Cube project, board configuration, linker script, implemented worker task, or hardware validation in this repository. Revisit the plan if a different STM32 is chosen.

## 1. Select hardware and budget memory

Record the board/part, available shared and external RAM, display controller/interface, resolution, color encoding, and refresh requirements. The target is six-color e-ink, approximately 8×6 inches; physical dimensions do not yet establish a pixel count. Linux now uses a fixed six-color preview, with no historical RGB ink.

Budget logical tape and occupancy at one byte per cell each, plus any chosen display buffers, core-private stacks, scheduler/ant state, and diagnostics. The core contains no RGB storage. Choose panel buffers independently: portable `render_codes()` can map indices to byte codes, while packing and hardware transfer remain backend responsibilities. Do not assume the Linux default 127-page capture ring fits on the MCU.

## 2. Refactor the source boundaries

Separate portable scheduling/token policy from Linux locking, waits, clocks, and notifications. Add a compile-time occupancy interface for Linux CAS and STM32 HSEM without a hot-path function-pointer requirement. Presentation is already outside the core: `RenderFrame` carries a stable snapshot of logical indices, and the portable conversion functions have no SDL/RTOS dependencies. Move host entropy behind a platform provider. See [../RENDERING.md](../RENDERING.md) for the rendering boundary.

Define ownership and synchronization for every shared field, including flags, leases, token health, positions, RNG state, counters, and reset state. Current `_Atomic` fields and pthread objects cannot simply be copied into a shared linker section as a complete protocol. Audit atomic widths and operations with the selected toolchain.

Use a defined cross-image representation for rule references. The Linux `Ant` contains a pointer into its image's static rule catalogue; separately linked CM7/CM4 images must not assume those pointers are interchangeable. Rule indices provide an existing starting point.

Resolve the Linux collision-loser continuation issue and specify population-pressure behavior before treating the interpreter as the embedded behavioral contract. See [../SPEC.md](../SPEC.md).

## 3. Create the firmware projects

Generate separate CM7 and CM4 applications with the appropriate Cube HAL/startup support and a FreeRTOS instance on each core. Add a worker task per core; CM7 also owns control/lifecycle and later display refresh. Supply build configuration for the shared core and the selected platform backend.

Implement the declared time, entropy, notify, and wait functions in `turmite_stm32_port.h`. A work wait must allow token-driven progress when no external notification arrives; work can become eligible solely through elapsed time.

## 4. Assign HSEM and notification ownership

Proposed allocation:

| HSEM | Purpose | Status |
| --- | --- | --- |
| 0..15 | Occupancy stripes | Used by the unintegrated helper |
| 16 | Scheduler/control lock | Planned |
| 17 | CM7 → CM4 work notification | Planned |
| 18 | CM4 → CM7 completion notification | Planned |
| 19 | Lifecycle/reset notification | Planned |
| 20..31 | Reserved | Unassigned |

Reconcile this allocation with generated boot synchronization and other peripherals before use. Enable the required peripheral clocks, interrupts, and notification handling in the board project.

Occupancy critical sections protect one byte. Keep tape writes outside those sections to preserve independent world updates. Define which local tasks/interrupts can enter HSEM-protected paths and how local exclusion works; the current helper only takes/releases HSEM and yields on contention. It is not an interrupt-safe or general-purpose scheduler lock implementation.

## 5. Reserve and initialize shared SRAM

Both linker scripts must reserve the same physical region with an agreed layout, size, and alignment for `.turmite_shared`. Configure the CM7 MPU attributes before accessing that region with caching enabled or releasing CM4. Provide actual shared arrays/objects; the current header supplies only an attribute.

Shared objects include tape, occupancy, and the portions of ant/scheduler/control state needed by both cores. Keep private execution state and stacks outside the shared region. If display DMA is used, define its memory placement and cache/ownership protocol separately.

Arrange startup so CM4 initialization cannot clear state after CM7 has initialized it. Cross-image linker placement and startup clearing must be designed together, not inferred from matching section names.

## 6. Establish boot and reset ownership

The proposed CM7 sequence is:

1. Initialize clocks/peripherals and configure memory attributes.
2. Initialize shared world, occupancy, and control state.
3. Obtain seed entropy and initialize the universe/scheduler.
4. Signal that shared state is ready for CM4.
5. Start worker/control tasks under the agreed boot protocol.

CM4 must wait for readiness before touching Turmite state. Define how both cores stop/quiesce before shared-state reinitialization. Linux currently restarts in software; a genuine watchdog/reset lifecycle remains a hardware design and implementation task, including how to achieve the desired five-minute lifetime.

## 7. Provide a common timebase and entropy

Seed once per new universe from the selected MCU entropy source, then use the existing LFSR algorithms for runtime variation. Explicit seeds can help reproduce initialization, but core timing still permits divergent execution.

The declared time API returns monotonic `uint64_t` microseconds. Both workers' timestamps must be comparable if they update one shared scheduler. If implemented with a 32-bit timer, account for wrap and extend it or change the accounting contract deliberately. A 1 MHz 32-bit counter wraps after about 71.6 minutes; repeated five-minute universes alone do not ensure the hardware timer resets. The Linux scheduler's current `now_us <= last_us` check is not wrap-safe for a raw wrapping counter.

## 8. Validate without the display

First establish through SWD/UART that:

- both workers execute against the same tape and agreed rule catalogue;
- leases prevent simultaneous execution of one ant;
- occupancy and collision/mutation behavior match the chosen contract;
- counters and token accrual remain valid across cores and time boundaries;
- both cores obey initialization, stop, and restart ownership;
- the intended lifecycle can restart safely.

Then add the display as an independently scheduled observer. Its refresh cadence, snapshot strategy, conversion buffers, and transport/DMA behavior should follow the selected display requirements. The simulation must not assume a 60 Hz SDL loop or an e-paper refresh cycle.
