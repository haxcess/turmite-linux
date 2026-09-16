#ifndef TURMITE_STM32_MEMORY_H
#define TURMITE_STM32_MEMORY_H

/*
 * Objects visible to both CM7 and CM4 must be placed in a linker section that
 * both images map to the same physical SRAM address. Configure that region as
 * non-cacheable on CM7 before CM4 is released from boot synchronization.
 *
 * The exact SRAM bank/address belongs in the CubeIDE linker scripts because it
 * depends on the selected H745/H747 part and the rest of the application.
 */
#if defined(__GNUC__)
#define TURMITE_SHARED __attribute__((section(".turmite_shared"), aligned(32)))
#else
#define TURMITE_SHARED
#endif

#endif
