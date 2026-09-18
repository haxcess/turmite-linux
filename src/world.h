#ifndef TURMITE_WORLD_H
#define TURMITE_WORLD_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#include "colors.h"

/* Simulation tape only. Presentation owns its buffers and color mapping. */
typedef struct {
    int width;
    int height;
    size_t cells;
    _Atomic uint8_t *data;
} World;

int world_init(World *world, int width, int height);
void world_destroy(World *world);
void world_clear(World *world);
uint8_t world_load(const World *world, size_t index);
void world_store(World *world, size_t index, uint8_t value);

/* Caller must supply a valid cell index. One independent relaxed byte write. */
static inline void world_store_cell(World *world, size_t index, uint8_t value)
{
    atomic_store_explicit(&world->data[index], (uint8_t)(value % TURMITE_COLORS),
                          memory_order_relaxed);
}

#endif
