# STM32H7 port

This directory is the start of the embedded port of Turmite Universe.

The first target is the dual-core STM32H745/H747 family (Cortex-M7 + Cortex-M4). The Linux implementation remains the reference model for turmite rules and scheduler behaviour; STM32-specific code supplies timing, inter-core synchronization, FreeRTOS workers, and eventually the e-paper display driver.

## Core split

### CM7

- owns universe initialization and the global scheduler policy
- runs one ant worker
- controls population changes and five-minute lifecycle
- eventually owns the e-paper refresh task
- initializes shared-memory regions before releasing CM4

### CM4

- runs the second ant worker
- requests/receives logical ant work from the shared scheduler state
- does not own display or universe lifecycle policy

The two physical cores are intentionally asymmetric. Their different clock rates are part of the physical execution environment rather than something the simulation hides.

## Shared memory

Shared cross-core state belongs in a non-cacheable SRAM region configured by the MPU/linker scripts. This includes:

- six-color world cells
- read-head occupancy index
- ant state required by the other core
- scheduler/mailbox state

Core-private stacks, temporary ant execution state, and display conversion buffers should stay in core-local/cacheable RAM where possible.

The six-color world remains deliberately racy: writes are not serialized and last-observed write wins. Read-head occupancy is different: only one ant may own a cell. The initial STM32 implementation protects occupancy claims with 32 striped hardware semaphores (HSEM). A cell hashes to one HSEM stripe, so unrelated cells can still be claimed concurrently.

## FreeRTOS shape

The first implementation should use a FreeRTOS instance on each core rather than attempting to emulate a single SMP kernel.

CM7 tasks:

- `turmite_worker_task`
- `turmite_control_task`
- later: `display_task`

CM4 tasks:

- `turmite_worker_task`

Inter-core notification should use HSEM interrupt/notification mechanisms or a very small shared mailbox. OpenAMP is intentionally avoided for the initial port.

## Porting rule

Do not copy Linux synchronization mechanisms mechanically.

Portable concepts:

- rules and rule interpreter
- Galois LFSR
- fixed-point tokens
- WFQ policy and minimum-service batching
- quantum execution
- collision health/mutation rules
- O(1) occupancy index

Platform-specific mechanisms:

- pthread mutex/condvar -> FreeRTOS + HSEM/notifications
- `clock_gettime()` -> hardware timer / RTOS monotonic time
- host entropy -> MCU hardware entropy source
- SDL -> e-paper driver
- Linux atomics on shared cacheable memory -> explicitly designed STM32 shared-memory protocol

## Next implementation step

The next code change should move occupancy claim/release operations behind a small platform interface. Linux will keep its atomic-CAS implementation; STM32 will use the HSEM-striped implementation in `src/turmite_stm32_shared.c`.
