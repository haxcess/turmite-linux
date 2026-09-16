#ifndef TURMITE_STM32_SHARED_H
#define TURMITE_STM32_SHARED_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Shared read-head occupancy for the dual-core STM32 target.
 *
 * The world tape itself is intentionally unsynchronized. Occupancy is not:
 * two read-heads may not own one cell at the same time. STM32 HSEM channels
 * are used as striped locks around the shared occupancy byte array.
 */

#define TURMITE_STM32_HSEM_STRIPES 16u
#define TURMITE_OCCUPANCY_EMPTY 0u

/* One byte per world cell: 0 = empty, 1..32 = ant index + 1. */
typedef struct {
    volatile uint8_t *cells;
    size_t cell_count;
} TurmiteStm32Occupancy;

/* Initialize the descriptor. Storage itself must already be in shared,
 * non-cacheable SRAM and zeroed by CM7 before CM4 starts using it. */
void turmite_stm32_occupancy_init(TurmiteStm32Occupancy *occupancy,
                                  volatile uint8_t *cells,
                                  size_t cell_count);

/* Try to claim an empty cell for owner_id. Returns true on success.
 * On failure, resident_id receives the current owner. */
bool turmite_stm32_occupancy_claim(TurmiteStm32Occupancy *occupancy,
                                   size_t cell,
                                   uint8_t owner_id,
                                   uint8_t *resident_id);

/* Replace expected_owner with new_owner while holding the stripe lock.
 * Used by collision resolution when the healthier ant takes the cell. */
bool turmite_stm32_occupancy_replace(TurmiteStm32Occupancy *occupancy,
                                     size_t cell,
                                     uint8_t expected_owner,
                                     uint8_t new_owner);

/* Release a cell only if it is still owned by owner_id. */
bool turmite_stm32_occupancy_release(TurmiteStm32Occupancy *occupancy,
                                     size_t cell,
                                     uint8_t owner_id);

uint8_t turmite_stm32_occupancy_load(const TurmiteStm32Occupancy *occupancy,
                                     size_t cell);

#endif
