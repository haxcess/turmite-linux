#include "turmite_stm32_shared.h"

#include "FreeRTOS.h"
#include "task.h"
#include "stm32h7xx_hal.h"

/* HSEM 0..15 are reserved for occupancy stripes. Higher channels remain
 * available for scheduler, lifecycle, and display notifications. */
static inline uint32_t stripe_for_cell(size_t cell)
{
    return (uint32_t)(cell & (TURMITE_STM32_HSEM_STRIPES - 1u));
}

static void stripe_lock(size_t cell)
{
    const uint32_t sem = stripe_for_cell(cell);
    while (HAL_HSEM_FastTake(sem) != HAL_OK) {
        /* Occupancy critical sections are only a byte load/store. Yield rather
         * than spin at full speed when the other core owns this stripe. */
        taskYIELD();
    }
    __DMB();
}

static void stripe_unlock(size_t cell)
{
    __DMB();
    HAL_HSEM_Release(stripe_for_cell(cell), 0u);
}

void turmite_stm32_occupancy_init(TurmiteStm32Occupancy *occupancy,
                                  volatile uint8_t *cells,
                                  size_t cell_count)
{
    occupancy->cells = cells;
    occupancy->cell_count = cell_count;
}

bool turmite_stm32_occupancy_claim(TurmiteStm32Occupancy *occupancy,
                                   size_t cell,
                                   uint8_t owner_id,
                                   uint8_t *resident_id)
{
    if (!occupancy || !occupancy->cells || cell >= occupancy->cell_count || owner_id == 0u)
        return false;

    stripe_lock(cell);
    const uint8_t resident = occupancy->cells[cell];
    const bool claimed = resident == TURMITE_OCCUPANCY_EMPTY;
    if (claimed)
        occupancy->cells[cell] = owner_id;
    stripe_unlock(cell);

    if (resident_id)
        *resident_id = resident;
    return claimed;
}

bool turmite_stm32_occupancy_replace(TurmiteStm32Occupancy *occupancy,
                                     size_t cell,
                                     uint8_t expected_owner,
                                     uint8_t new_owner)
{
    if (!occupancy || !occupancy->cells || cell >= occupancy->cell_count || new_owner == 0u)
        return false;

    stripe_lock(cell);
    const bool replaced = occupancy->cells[cell] == expected_owner;
    if (replaced)
        occupancy->cells[cell] = new_owner;
    stripe_unlock(cell);
    return replaced;
}

bool turmite_stm32_occupancy_release(TurmiteStm32Occupancy *occupancy,
                                     size_t cell,
                                     uint8_t owner_id)
{
    if (!occupancy || !occupancy->cells || cell >= occupancy->cell_count || owner_id == 0u)
        return false;

    stripe_lock(cell);
    const bool released = occupancy->cells[cell] == owner_id;
    if (released)
        occupancy->cells[cell] = TURMITE_OCCUPANCY_EMPTY;
    stripe_unlock(cell);
    return released;
}

uint8_t turmite_stm32_occupancy_load(const TurmiteStm32Occupancy *occupancy,
                                     size_t cell)
{
    if (!occupancy || !occupancy->cells || cell >= occupancy->cell_count)
        return TURMITE_OCCUPANCY_EMPTY;

    /* Diagnostic/non-owning reads do not need a semaphore. Ownership changes
     * must still go through claim/replace/release. Shared SRAM is non-cacheable. */
    __DMB();
    return occupancy->cells[cell];
}
