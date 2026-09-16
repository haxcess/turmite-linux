#ifndef TURMITE_STM32_PORT_H
#define TURMITE_STM32_PORT_H

#include <stdint.h>

/*
 * Small hardware-facing surface used by the embedded port. The turmite core
 * should depend on these concepts rather than HAL/FreeRTOS details directly.
 */

/* Monotonic microsecond counter used for token accrual. */
uint64_t turmite_stm32_now_us(void);

/* Hardware entropy is only needed to seed a universe. Runtime randomness uses
 * the existing deterministic Galois LFSRs so a captured seed can be replayed. */
uint32_t turmite_stm32_entropy32(void);

/* Notify the peer core that scheduler/control work is available. */
void turmite_stm32_notify_peer(void);

/* Block/yield until local work or an inter-core notification is available. */
void turmite_stm32_wait_for_work(void);

#endif
