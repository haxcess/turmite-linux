# STM32H745/H747 integration checklist

This file describes the first hardware bring-up. It intentionally leaves Cube-generated HAL/startup files outside the repository until a specific board is selected.

## 1. Create the dual-core project

Use STM32CubeIDE/CubeMX with an STM32H745/H747 dual-core target. Generate separate CM7 and CM4 applications.

Enable FreeRTOS on both cores. The first port uses one worker task per core; CM7 additionally owns control/lifecycle work and later the display task.

## 2. Enable HSEM

Enable the HSEM interrupt for both cores.

Initial channel allocation:

| HSEM | Purpose |
| --- | --- |
| 0..15 | striped occupancy locks |
| 16 | scheduler/control lock |
| 17 | CM7 -> CM4 work notification |
| 18 | CM4 -> CM7 completion notification |
| 19 | lifecycle/reset notification |
| 20..31 | reserved |

The occupancy critical section protects only one byte in the derived occupancy index. Do not put the six-color world write behind HSEM; world races are intentional.

## 3. Reserve shared SRAM

Create a `.turmite_shared` linker section at the same physical SRAM address in both CM7 and CM4 linker scripts.

Put only cross-core state there:

- world cell array
- occupancy byte array
- cross-core scheduler/mailbox state
- ant fields that must be observed by the other core

Keep stacks, temporary execution state, and other core-private objects out of this region.

Configure the shared region as non-cacheable on CM7 with the MPU before CM4 begins using it. This avoids making every shared access depend on explicit D-cache clean/invalidate operations.

## 4. Boot ownership

CM7 is the bootstrap core for Turmite Universe:

1. initialize clocks/peripherals
2. configure the MPU shared-memory region
3. zero shared world/occupancy/control structures
4. seed the universe RNG
5. initialize the universe and scheduler state
6. release/notify CM4
7. start CM7 FreeRTOS tasks

CM4 must not touch shared Turmite state until CM7 signals that initialization is complete.

## 5. Time and entropy

Use hardware entropy once at universe creation, then continue with the existing Galois LFSRs.

For scheduler token accrual, provide a monotonic microsecond-scale counter. A free-running 32-bit timer configured to 1 MHz is a straightforward first implementation and wraps after about 71 minutes, comfortably longer than the five-minute universe lifetime. Wrap-safe subtraction should still be used.

## 6. First bring-up milestone

Do not connect the e-paper panel yet.

The first hardware milestone is:

- CM7 and CM4 each execute ant work
- both mutate one shared six-color world
- occupancy claims use HSEM stripes
- collision/mutation counters increase
- five-minute lifecycle can restart the universe
- state can be inspected over SWD/UART

Once this is stable, add the display as a slow observer of the existing world rather than coupling display refresh to simulation execution.

## 7. Source boundary still to refactor

The current Linux `scheduler.c` still combines WFQ policy with pthread mutex/condition-variable mechanics. The next porting change should split:

- portable scheduler policy/token accounting
- Linux wait/notify/locking backend
- STM32 FreeRTOS/HSEM backend

Likewise, occupancy operations should be called through a compile-time platform interface so `ant.c` remains shared between Linux and STM32 without a function-pointer cost in the instruction hot path.
